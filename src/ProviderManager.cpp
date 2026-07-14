#include "ProviderManager.h"
#include "Utils.h"
#include <algorithm>

bool ProviderManager::LoadProviders(const std::function<std::string(const std::string&)>& httpGet,
                                    const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson) {
  std::string response = httpGet("/api/providers");
  if (response.empty()) return false;

  rapidjson::Document document;
  if (!parseJson(response, document)) return false;
  if (!document.HasMember("providers") || !document["providers"].IsArray()) return false;

  std::vector<UltimateProvider> newProviders;
  std::map<std::string, int> newProviderIdMap;

  for (const auto& provider : document["providers"].GetArray()) {
    if (provider.IsObject() && provider.HasMember("name")) {
      UltimateProvider p;
      p.name = provider["name"].GetString();
      p.label = (provider.HasMember("label") && provider["label"].IsString()) ? provider["label"].GetString() : p.name;
      p.country = (provider.HasMember("country") && provider["country"].IsString()) ? provider["country"].GetString() : "";
      p.logo = (provider.HasMember("logo") && provider["logo"].IsString()) ? provider["logo"].GetString() : "";
      p.enabled = IsProviderEnabled(p.name);
      p.uniqueId = Utils::GenerateProviderUniqueId(p.name);

      newProviders.push_back(p);
      newProviderIdMap[p.name] = p.uniqueId;
    }
  }

  std::unique_lock<std::shared_mutex> lock(m_dataMutex);
  m_providers = std::move(newProviders);
  m_providerIdMap = std::move(newProviderIdMap);

  return true;
}

bool ProviderManager::GetProviders(kodi::addon::PVRProvidersResultSet& results) {
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

bool ProviderManager::IsProviderEnabled(const std::string& provider) {
  return true;
}