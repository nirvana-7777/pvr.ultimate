#include "ProviderManager.h"
#include "Utils.h"
#include <algorithm>

bool ProviderManager::LoadProviders(const std::function<std::string(const std::string&)>& httpGet,
                                    const std::function<bool(const std::string&, nlohmann::json&)>& parseJson) {
  std::string response = httpGet("/api/providers");
  if (response.empty()) return false;

  nlohmann::json document;
  if (!parseJson(response, document)) return false;
  if (!document.contains("providers") || !document["providers"].is_array()) return false;

  std::vector<UltimateProvider> newProviders;
  std::map<std::string, int> newProviderIdMap;

  for (const auto& provider : document["providers"]) {
    if (provider.is_object() && provider.contains("name") && provider["name"].is_string()) {
      UltimateProvider p;
      p.name = provider["name"].get<std::string>();
      p.label = (provider.contains("label") && provider["label"].is_string()) ? provider["label"].get<std::string>() : p.name;
      p.country = (provider.contains("country") && provider["country"].is_string()) ? provider["country"].get<std::string>() : "";
      p.logo = (provider.contains("logo") && provider["logo"].is_string()) ? provider["logo"].get<std::string>() : "";
      p.enabled = (provider.contains("enabled") && provider["enabled"].is_boolean()) ? provider["enabled"].get<bool>() : true;
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