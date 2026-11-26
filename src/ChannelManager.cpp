#include "ChannelManager.h"
#include "Utils.h"
#include <kodi/General.h>

// Standard library includes
#include <vector>
#include <string>
#include <map>
#include <set>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <algorithm>

// nlohmann/json include
#include <nlohmann/json.hpp>

bool ChannelManager::LoadChannels(const std::vector<UltimateProvider>& providers,
                                  const std::function<std::string(const std::string&)>& httpGet,
                                  const std::function<bool(const std::string&, nlohmann::json&)>& parseJson,
                                  std::vector<std::string>* failedProviders) {
  std::vector<UltimateChannel> newChannels;
  std::map<int, ChannelLookupInfo> newLookup;
  std::vector<std::string> failed;

  // Use each provider's position in the (name-sorted, see ProviderManager)
  // providers vector as its channel-number offset index, rather than a
  // counter that only increments for enabled providers. Using the position
  // in the full list means disabling/enabling a provider does not shift the
  // channel numbers (and therefore clientChannelUid values used by existing
  // timers/recordings) of any other provider's channels.
  for (size_t providerIndex = 0; providerIndex < providers.size(); ++providerIndex) {
    const auto& provider = providers[providerIndex];
    if (provider.enabled) {
      if (!LoadChannelsForProvider(provider.name, provider.uniqueId, static_cast<int>(providerIndex),
                                   httpGet, parseJson, newChannels, newLookup)) {
        failed.push_back(provider.name);
      }
    }
  }

  std::unique_lock<std::shared_mutex> lock(m_dataMutex);

  // NEW: a provider that gave no usable answer THIS time keeps the channels it
  // had before (a reload must never make channels vanish). On the very first
  // load there is nothing to keep.
  if (!failed.empty()) {
    for (const auto& oldChannel : m_channels) {
      if (std::find(failed.begin(), failed.end(), oldChannel.provider) == failed.end()) continue;
      newChannels.push_back(oldChannel);
      auto it = m_channelLookup.find(oldChannel.uid);
      if (it != m_channelLookup.end()) newLookup[oldChannel.uid] = it->second;
    }
  }

  m_channels = std::move(newChannels);
  m_channelLookup = std::move(newLookup);
  HideQualityVariants();
  m_channelIndex.clear();
  for (size_t i = 0; i < m_channels.size(); ++i) {
    m_channelIndex[m_channels[i].uid] = i;
  }

  if (failedProviders) *failedProviders = std::move(failed);  // NEW
  return !m_channels.empty();
}

int ChannelManager::ComputeChannelUid(int providerOffset, int legacyUid, int uniqueChannelNumber) {
  // iUniqueId is persisted by Kodi (channel settings, favourites, timers, recordings).
  // Do NOT change this scheme once shipped.
  if (uniqueChannelNumber > 0 && uniqueChannelNumber < PROVIDER_OFFSET_MULTIPLIER) {
    return providerOffset + uniqueChannelNumber;
  }
  return legacyUid;  // backend without UniqueChannelNumber: unchanged behaviour
}

bool ChannelManager::LoadChannelsForProvider(const std::string& provider, int providerUniqueId, int providerIndex,
                                             const std::function<std::string(const std::string&)>& httpGet,
                                             const std::function<bool(const std::string&, nlohmann::json&)>& parseJson,
                                             std::vector<UltimateChannel>& outChannels,
                                             std::map<int, ChannelLookupInfo>& outLookup) {
  std::string url = "/api/providers/" + Utils::UrlPathEncode(provider) + "/channels";
  std::string response = httpGet(url);
  if (response.empty()) {
    kodi::Log(ADDON_LOG_WARNING, "Empty response from %s", Utils::RedactUrl(url).c_str());
    return false;  // CHANGED
  }

  nlohmann::json document;
  if (!parseJson(response, document)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to parse channels response from %s", provider.c_str());
    return false;  // CHANGED
  }
  if (!document.contains("channels") || !document["channels"].is_array()) {
    kodi::Log(ADDON_LOG_ERROR, "Missing channels array in response from %s", provider.c_str());
    return false;  // CHANGED
  }

  // Store reference to avoid repeated map lookups / for readability
  const auto& channelsArray = document["channels"];

  int providerOffset = providerIndex * PROVIDER_OFFSET_MULTIPLIER;
  int nextFallbackNumber = providerOffset + 1;

  // First pass: find the maximum explicit channel number
  int maxExplicitNumber = providerOffset;
  for (const auto& channelJson : channelsArray) {
    if (channelJson.contains("ChannelNumber") && channelJson["ChannelNumber"].is_number_integer()) {
      int num = channelJson["ChannelNumber"].get<int>() + providerOffset;
      if (num > maxExplicitNumber) maxExplicitNumber = num;
    }
  }

  if (maxExplicitNumber > providerOffset) {
    nextFallbackNumber = maxExplicitNumber + 1;
  }

  // uids already handed out for this provider (guards against duplicate numbers from a backend)
  std::set<int> usedUids;
  std::vector<std::pair<size_t, ChannelLookupInfo>> duplicates;  // index into outChannels

  // Second pass: build channel objects
  for (const auto& channelJson : channelsArray) {
    UltimateChannel channel;
    channel.provider = provider;
    channel.providerUniqueId = providerUniqueId;

    channel.channelName = (channelJson.contains("Name") && channelJson["Name"].is_string())
                          ? channelJson["Name"].get<std::string>() : "Unknown";
    channel.channelId = (channelJson.contains("Id") && channelJson["Id"].is_string())
                        ? channelJson["Id"].get<std::string>() : "";
    channel.iconPath = (channelJson.contains("LogoUrl") && channelJson["LogoUrl"].is_string())
                       ? channelJson["LogoUrl"].get<std::string>() : "";

    if (channelJson.contains("ChannelNumber") && channelJson["ChannelNumber"].is_number_integer()) {
      channel.channelNumber = channelJson["ChannelNumber"].get<int>() + providerOffset;
      if (channel.channelNumber >= nextFallbackNumber) {
        nextFallbackNumber = channel.channelNumber + 1;
      }
    } else {
      channel.channelNumber = nextFallbackNumber++;
    }

    channel.uniqueId = provider + ":" + channel.channelId;

    const int uniqueChannelNumber =
        (channelJson.contains("UniqueChannelNumber") && channelJson["UniqueChannelNumber"].is_number_integer())
            ? channelJson["UniqueChannelNumber"].get<int>() : 0;
    channel.uid = ComputeChannelUid(providerOffset, channel.channelNumber, uniqueChannelNumber);
    const bool duplicateUid = !usedUids.insert(channel.uid).second;
    channel.mode = (channelJson.contains("Mode") && channelJson["Mode"].is_string())
                   ? channelJson["Mode"].get<std::string>() : "live";
    channel.sessionManifest = (channelJson.contains("SessionManifest") && channelJson["SessionManifest"].is_boolean())
                              ? channelJson["SessionManifest"].get<bool>() : false;
    channel.manifest = (channelJson.contains("Manifest") && channelJson["Manifest"].is_string())
                       ? channelJson["Manifest"].get<std::string>() : "";
    channel.manifestScript = (channelJson.contains("ManifestScript") && channelJson["ManifestScript"].is_string())
                             ? channelJson["ManifestScript"].get<std::string>() : "";
    channel.useCdm = (channelJson.contains("UseCdm") && channelJson["UseCdm"].is_boolean())
                     ? channelJson["UseCdm"].get<bool>() : true;
    channel.cdmMode = (channelJson.contains("CdmMode") && channelJson["CdmMode"].is_string())
                      ? channelJson["CdmMode"].get<std::string>() : "external";
    channel.contentType = (channelJson.contains("ContentType") && channelJson["ContentType"].is_string())
                          ? channelJson["ContentType"].get<std::string>() : "LIVE";
    channel.country = (channelJson.contains("Country") && channelJson["Country"].is_string())
                      ? channelJson["Country"].get<std::string>() : "";
    channel.language = (channelJson.contains("Language") && channelJson["Language"].is_string())
                       ? channelJson["Language"].get<std::string>() : "en";
    channel.streamingFormat = (channelJson.contains("StreamingFormat") && channelJson["StreamingFormat"].is_string())
                              ? channelJson["StreamingFormat"].get<std::string>() : "";
    channel.quality = (channelJson.contains("Quality") && channelJson["Quality"].is_string())
                      ? channelJson["Quality"].get<std::string>() : "";

    // "ChannelType" is the field the backend actually sends for this purpose
    // (RecordingManager reads the same field from its own "channels" data).
    // IsRadio/contentType are kept as fallbacks in case a given provider
    // integration only populates one of the others.
    if (channelJson.contains("ChannelType") && channelJson["ChannelType"].is_string()) {
      channel.isRadio = (channelJson["ChannelType"].get<std::string>() == "RADIO");
    } else if (channelJson.contains("IsRadio") && channelJson["IsRadio"].is_boolean()) {
      channel.isRadio = channelJson["IsRadio"].get<bool>();
    } else {
      channel.isRadio = (channel.contentType == "RADIO");
    }

    ChannelLookupInfo lookupInfo;
    lookupInfo.provider = provider;
    lookupInfo.channelId = channel.channelId;
    lookupInfo.catchupHours = (channelJson.contains("CatchupHours") && channelJson["CatchupHours"].is_number_integer())
                              ? channelJson["CatchupHours"].get<int>() : 0;

    if (duplicateUid) {
      // Should not happen. A silent collision would make two channels share one Kodi uid
      // (wrong manifest/licence), so such channels get a fresh uid above everything else
      // after the loop; that way no other channel's uid is shifted.
      kodi::Log(ADDON_LOG_WARNING, "Duplicate channel uid %d from %s (channel '%s')",
                channel.uid, provider.c_str(), channel.channelName.c_str());
      duplicates.push_back({outChannels.size(), lookupInfo});
    } else {
      outLookup[channel.uid] = lookupInfo;
    }
    outChannels.push_back(channel);
  }

  if (!duplicates.empty()) {
    int nextUid = *usedUids.rbegin();
    for (auto& [index, info] : duplicates) {
      do { ++nextUid; } while (!usedUids.insert(nextUid).second);
      outChannels[index].uid = nextUid;
      outLookup[nextUid] = info;
    }
  }
  return true;  // CHANGED
}

void ChannelManager::HideQualityVariants() {
  auto rank = [](const std::string& quality) {
    if (quality == "SD") return 1;
    if (quality == "HD") return 2;
    if (quality == "UHD" || quality == "4K") return 3;
    return 0;
  };
  const bool preferHigher = m_preferHigherResolution.load();
  std::map<int, size_t> preferred;  // channel number -> index into m_channels
  for (size_t i = 0; i < m_channels.size(); ++i) {
    m_channels[i].isHidden = false;
    auto it = preferred.find(m_channels[i].channelNumber);
    if (it == preferred.end()) {
      preferred.emplace(m_channels[i].channelNumber, i);
      continue;
    }
    const int candidate = rank(m_channels[i].quality);
    const int current = rank(m_channels[it->second].quality);
    if (preferHigher ? candidate > current : candidate < current) {
      m_channels[it->second].isHidden = true;
      it->second = i;
    } else {
      m_channels[i].isHidden = true;
    }
  }
}

int ChannelManager::GetChannelsAmount() const {
  std::shared_lock<std::shared_mutex> lock(m_dataMutex);
  return static_cast<int>(m_channels.size());
}

bool ChannelManager::GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results) const {
  std::shared_lock<std::shared_mutex> lock(m_dataMutex);
  for (const auto& channel : m_channels) {
    if (channel.isRadio == radio) {
      kodi::addon::PVRChannel kodiChannel;
      kodiChannel.SetUniqueId(channel.uid);
      kodiChannel.SetIsRadio(channel.isRadio);
      kodiChannel.SetChannelNumber(channel.channelNumber);
      kodiChannel.SetChannelName(channel.channelName);
      kodiChannel.SetIconPath(channel.iconPath);
      kodiChannel.SetIsHidden(channel.isHidden);
      kodiChannel.SetClientProviderUid(channel.providerUniqueId);
      results.Add(kodiChannel);
    }
  }
  return true;
}

bool ChannelManager::GetChannelInfo(int channelUid, std::string& provider, std::string& channelId, int& catchupHours) const {
  std::shared_lock<std::shared_mutex> lock(m_dataMutex);
  auto it = m_channelLookup.find(channelUid);
  if (it == m_channelLookup.end()) return false;
  provider = it->second.provider;
  channelId = it->second.channelId;
  catchupHours = it->second.catchupHours;
  return true;
}

bool ChannelManager::GetChannelByUid(int channelUid, UltimateChannel& channel) const {
  std::shared_lock<std::shared_mutex> lock(m_dataMutex);
  auto it = m_channelIndex.find(channelUid);
  if (it != m_channelIndex.end() && it->second < m_channels.size()) {
    channel = m_channels[it->second];
    return true;
  }
  return false;
}