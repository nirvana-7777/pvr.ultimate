#include "ChannelManager.h"
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

// RapidJSON includes
#include <rapidjson/document.h>

bool ChannelManager::LoadChannels(const std::vector<UltimateProvider>& providers,
                                  const std::function<std::string(const std::string&)>& httpGet,
                                  const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson) {
  std::vector<UltimateChannel> newChannels;
  std::map<int, ChannelLookupInfo> newLookup;

  int providerIndex = 0;
  for (const auto& provider : providers) {
    if (provider.enabled) {
      LoadChannelsForProvider(provider.name, providerIndex, httpGet, parseJson, newChannels, newLookup);
      providerIndex++;
    }
  }

  std::unique_lock<std::shared_mutex> lock(m_dataMutex);
  m_channels = std::move(newChannels);
  m_channelLookup = std::move(newLookup);

  return !m_channels.empty();
}

void ChannelManager::LoadChannelsForProvider(const std::string& provider, int providerIndex,
                                             const std::function<std::string(const std::string&)>& httpGet,
                                             const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson,
                                             std::vector<UltimateChannel>& outChannels,
                                             std::map<int, ChannelLookupInfo>& outLookup) {
  std::string response = httpGet("/api/providers/" + provider + "/channels");
  if (response.empty()) return;

  rapidjson::Document document;
  if (!parseJson(response, document)) return;
  if (!document.HasMember("channels") || !document["channels"].IsArray()) return;

  // Store reference to avoid calling GetArray() multiple times
  const auto& channelsArray = document["channels"].GetArray();

  int providerOffset = providerIndex * PROVIDER_OFFSET_MULTIPLIER;
  int nextFallbackNumber = providerOffset + 1;

  // First pass: find the maximum explicit channel number
  int maxExplicitNumber = providerOffset;
  for (const auto& channelJson : channelsArray) {
    if (channelJson.HasMember("ChannelNumber") && channelJson["ChannelNumber"].IsInt()) {
      int num = channelJson["ChannelNumber"].GetInt() + providerOffset;
      if (num > maxExplicitNumber) maxExplicitNumber = num;
    }
  }

  if (maxExplicitNumber > providerOffset) {
    nextFallbackNumber = maxExplicitNumber + 1;
  }

  // Second pass: build channel objects
  for (const auto& channelJson : channelsArray) {
    UltimateChannel channel;
    channel.provider = provider;

    channel.channelName = (channelJson.HasMember("Name") && channelJson["Name"].IsString())
                          ? channelJson["Name"].GetString() : "Unknown";
    channel.channelId = (channelJson.HasMember("Id") && channelJson["Id"].IsString())
                        ? channelJson["Id"].GetString() : "";
    channel.iconPath = (channelJson.HasMember("LogoUrl") && channelJson["LogoUrl"].IsString())
                       ? channelJson["LogoUrl"].GetString() : "";

    if (channelJson.HasMember("ChannelNumber") && channelJson["ChannelNumber"].IsInt()) {
      channel.channelNumber = channelJson["ChannelNumber"].GetInt() + providerOffset;
      if (channel.channelNumber >= nextFallbackNumber) {
        nextFallbackNumber = channel.channelNumber + 1;
      }
    } else {
      channel.channelNumber = nextFallbackNumber++;
    }

    channel.uniqueId = provider + ":" + channel.channelId;
    channel.mode = (channelJson.HasMember("Mode") && channelJson["Mode"].IsString())
                   ? channelJson["Mode"].GetString() : "live";
    channel.sessionManifest = (channelJson.HasMember("SessionManifest") && channelJson["SessionManifest"].IsBool())
                              ? channelJson["SessionManifest"].GetBool() : false;
    channel.manifest = (channelJson.HasMember("Manifest") && channelJson["Manifest"].IsString())
                       ? channelJson["Manifest"].GetString() : "";
    channel.manifestScript = (channelJson.HasMember("ManifestScript") && channelJson["ManifestScript"].IsString())
                             ? channelJson["ManifestScript"].GetString() : "";
    channel.useCdm = (channelJson.HasMember("UseCdm") && channelJson["UseCdm"].IsBool())
                     ? channelJson["UseCdm"].GetBool() : true;
    channel.cdmMode = (channelJson.HasMember("CdmMode") && channelJson["CdmMode"].IsString())
                      ? channelJson["CdmMode"].GetString() : "external";
    channel.contentType = (channelJson.HasMember("ContentType") && channelJson["ContentType"].IsString())
                          ? channelJson["ContentType"].GetString() : "LIVE";
    channel.country = (channelJson.HasMember("Country") && channelJson["Country"].IsString())
                      ? channelJson["Country"].GetString() : "";
    channel.language = (channelJson.HasMember("Language") && channelJson["Language"].IsString())
                       ? channelJson["Language"].GetString() : "en";
    channel.streamingFormat = (channelJson.HasMember("StreamingFormat") && channelJson["StreamingFormat"].IsString())
                              ? channelJson["StreamingFormat"].GetString() : "";

    if (channelJson.HasMember("IsRadio") && channelJson["IsRadio"].IsBool()) {
      channel.isRadio = channelJson["IsRadio"].GetBool();
    } else {
      channel.isRadio = (channel.contentType == "RADIO");
    }

    ChannelLookupInfo lookupInfo;
    lookupInfo.provider = provider;
    lookupInfo.channelId = channel.channelId;
    lookupInfo.catchupHours = (channelJson.HasMember("CatchupHours") && channelJson["CatchupHours"].IsInt())
                              ? channelJson["CatchupHours"].GetInt() : 0;

    outLookup[channel.channelNumber] = lookupInfo;
    outChannels.push_back(channel);
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
      kodiChannel.SetUniqueId(channel.channelNumber);
      kodiChannel.SetIsRadio(channel.isRadio);
      kodiChannel.SetChannelNumber(channel.channelNumber);
      kodiChannel.SetChannelName(channel.channelName);
      kodiChannel.SetIconPath(channel.iconPath);
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
  for (const auto& ch : m_channels) {
    if (ch.channelNumber == channelUid) {
      channel = ch;
      return true;
    }
  }
  return false;
}