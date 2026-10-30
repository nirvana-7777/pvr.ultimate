#include "ProviderManager.h"
#include "Utils.h"
#include <algorithm>
#include <kodi/Filesystem.h>
#include <kodi/General.h>

bool ProviderManager::LoadProviders(const std::function<std::string(const std::string&)>& httpGet,
                                    const std::function<bool(const std::string&, nlohmann::json&)>& parseJson) {
  std::string response = httpGet("/api/providers");
  if (response.empty()) return false;

  nlohmann::json document;
  if (!parseJson(response, document)) return false;

  // CHANGED: "providers" only lists ENABLED providers with a ready instance, so
  // using it as the offset index made channel numbers depend on which providers
  // are on. "all_providers" lists every provider with an "enabled" flag; fall
  // back to "providers" for backends that do not send it yet.
  const char* listKey = (document.contains("all_providers") && document["all_providers"].is_array())
                        ? "all_providers" : "providers";
  if (!document.contains(listKey) || !document[listKey].is_array()) return false;

  std::vector<UltimateProvider> newProviders;
  std::map<std::string, int> newProviderIdMap;

  for (const auto& provider : document[listKey]) {
    if (provider.is_object() && provider.contains("name") && provider["name"].is_string()) {
      UltimateProvider p;
      p.name = provider["name"].get<std::string>();
      p.label = (provider.contains("label") && provider["label"].is_string()) ? provider["label"].get<std::string>() : p.name;
      p.country = (provider.contains("country") && provider["country"].is_string()) ? provider["country"].get<std::string>() : "";
      p.logo = (provider.contains("logo") && provider["logo"].is_string()) ? provider["logo"].get<std::string>() : "";
      p.backendEnabled = (provider.contains("enabled") && provider["enabled"].is_boolean()) ? provider["enabled"].get<bool>() : true;
      p.enabled = p.backendEnabled;  // local deselection is applied below
      // NEW: idle | warming | ready | error (only present for enabled providers)
      if (provider.contains("channel_state") && provider["channel_state"].is_object() &&
          provider["channel_state"].contains("state") && provider["channel_state"]["state"].is_string()) {
        p.channelState = provider["channel_state"]["state"].get<std::string>();
      }
      p.uniqueId = Utils::GenerateProviderUniqueId(p.name);

      newProviders.push_back(p);
      newProviderIdMap[p.name] = p.uniqueId;
    }
  }

  // Sort by name (not backend response order) so that ChannelManager, which
  // uses each provider's position in this vector as a stable per-provider
  // channel-number offset, keeps that offset constant regardless of the
  // order the backend returns providers in, and regardless of which
  // providers are currently enabled. Without this, toggling one provider
  // off/on - or the backend simply reordering its response - reshuffles the
  // channelNumber (and therefore clientChannelUid) of every other provider's
  // channels, silently invalidating existing timers/recordings/EPG links.
  std::sort(newProviders.begin(), newProviders.end(),
            [](const UltimateProvider& a, const UltimateProvider& b) {
              return a.name < b.name;
            });

  std::unique_lock<std::shared_mutex> lock(m_dataMutex);
  for (auto& p : newProviders) {
    if (m_locallyDisabled.count(p.name)) p.enabled = false;
  }
  m_providers = std::move(newProviders);
  m_providerIdMap = std::move(newProviderIdMap);

  return true;
}

bool ProviderManager::GetProviders(kodi::addon::PVRProvidersResultSet& results) const {
  std::shared_lock<std::shared_mutex> lock(m_dataMutex);
  for (const auto& provider : m_providers) {
    if (provider.enabled) {
      kodi::addon::PVRProvider kodiProvider;
      kodiProvider.SetName(provider.label.empty() ? provider.name : provider.label);
      kodiProvider.SetType(PVR_PROVIDER_TYPE_IPTV);
      kodiProvider.SetIconPath(provider.logo);
      kodiProvider.SetUniqueId(provider.uniqueId);
      if (!provider.country.empty()) kodiProvider.SetCountries({provider.country});
      results.Add(kodiProvider);
    }
  }
  return true;
}

std::vector<UltimateProvider> ProviderManager::GetProvidersCopy() const {
  std::shared_lock<std::shared_mutex> lock(m_dataMutex);
  return m_providers;
}

int ProviderManager::GetProvidersAmount() const {
  std::shared_lock<std::shared_mutex> lock(m_dataMutex);
  return std::ranges::count_if(m_providers,
                               [](const UltimateProvider& p){ return p.enabled; });
}

std::string ProviderManager::GetProviderName(int uniqueId) const {
  std::shared_lock<std::shared_mutex> lock(m_dataMutex);
  for (const auto& provider : m_providers) {
    if (provider.uniqueId == uniqueId) return provider.name;
  }
  return "";
}

void ProviderManager::LoadLocalSelection(const std::string& filePath) {
  std::unique_lock<std::shared_mutex> lock(m_dataMutex);
  m_selectionPath = filePath;
  m_locallyDisabled.clear();
  m_hasSelection = false;

  if (!kodi::vfs::FileExists(filePath)) return;
  kodi::vfs::CFile file;
  if (!file.OpenFile(filePath)) return;

  std::string content;
  char buf[2048];
  ssize_t n;
  while ((n = file.Read(buf, sizeof(buf))) > 0) content.append(buf, static_cast<size_t>(n));
  file.Close();

  try {
    const auto doc = nlohmann::json::parse(content);
    m_hasSelection = doc.is_object();
    if (doc.contains("disabled") && doc["disabled"].is_array()) {
      for (const auto& v : doc["disabled"]) {
        if (v.is_string()) m_locallyDisabled.insert(v.get<std::string>());
      }
    }
  } catch (const std::exception& e) {
    kodi::Log(ADDON_LOG_ERROR, "Ignoring unreadable provider selection file: %s", e.what());
  }
}

std::set<std::string> ProviderManager::GetLocallyDisabled() const {
  std::shared_lock<std::shared_mutex> lock(m_dataMutex);
  return m_locallyDisabled;
}

bool ProviderManager::SetLocallyDisabled(const std::set<std::string>& disabled) {
  std::string path;
  {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    m_locallyDisabled = disabled;
    m_hasSelection = true;  // also if persisting fails below: keep the choice for this session
    path = m_selectionPath;
  }
  if (path.empty()) return false;

  const nlohmann::json doc = {{"disabled", disabled}};
  const std::string content = doc.dump(2);

  // Write to a temp file and rename over the target, so a crash/kill mid-write can
  // never leave a truncated selection file behind.
  const std::string tmpPath = path + ".tmp";
  kodi::vfs::CFile file;
  if (!file.OpenFileForWrite(tmpPath, true)) {
    kodi::Log(ADDON_LOG_ERROR, "Cannot write provider selection to %s", tmpPath.c_str());
    return false;
  }
  const bool written = file.Write(content.data(), content.size()) == static_cast<ssize_t>(content.size());
  file.Close();
  if (!written) {
    kodi::vfs::DeleteFile(tmpPath);
    return false;
  }
  if (!kodi::vfs::RenameFile(tmpPath, path)) {
    // Some VFS backends refuse to rename over an existing file: fall back to remove + rename.
    kodi::vfs::DeleteFile(path);
    if (!kodi::vfs::RenameFile(tmpPath, path)) {
      kodi::vfs::DeleteFile(tmpPath);
      return false;
    }
  }
  return true;
}

bool ProviderManager::HasLocalSelection() const {
  std::shared_lock<std::shared_mutex> lock(m_dataMutex);
  return m_hasSelection;
}
