#include "EPGManager.h"
#include <kodi/General.h>
#include <sstream>
#include <ctime>

bool EPGManager::GetEPGForChannel(int channelUid, time_t start, time_t end,
                                  const std::function<std::string(const std::string&)>& httpGet,
                                  const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson,
                                  const std::function<bool(int, UltimateChannel&)>& getChannelByUid,
                                  kodi::addon::PVREPGTagsResultSet& results) {
  std::string provider, channelId, country;
  UltimateChannel channel;

  if (!getChannelByUid(channelUid, channel)) return false;
  provider = channel.provider;
  channelId = channel.channelId;
  country = channel.country;

  std::ostringstream url;
  url << "/api/providers/" << provider
      << "/channels/" << channelId << "/epg"
      << "?start_time=" << start << "&end_time=" << end;
  if (!country.empty()) url << "&country=" << country;

  std::string response = httpGet(url.str());
  if (response.empty()) return false;

  rapidjson::Document document;
  if (!parseJson(response, document)) return false;

  if (document.HasMember("epg") && document["epg"].IsArray()) {
    for (const auto& epgItem : document["epg"].GetArray()) {
      kodi::addon::PVREPGTag tag;

      uint64_t eStart = (epgItem.HasMember("start") && epgItem["start"].IsUint64()) ? epgItem["start"].GetUint64() : 0;
      uint64_t eEnd = (epgItem.HasMember("end") && epgItem["end"].IsUint64()) ? epgItem["end"].GetUint64() : 0;

      unsigned int broadcastId = static_cast<unsigned int>(channelUid ^ (eStart << 16) ^ (eStart >> 16) ^ eEnd);

      tag.SetUniqueBroadcastId(broadcastId);
      tag.SetUniqueChannelId(channelUid);
      tag.SetStartTime(eStart);
      tag.SetEndTime(eEnd);

      if (epgItem.HasMember("title") && epgItem["title"].IsString())
        tag.SetTitle(epgItem["title"].GetString());
      if (epgItem.HasMember("plot") && epgItem["plot"].IsString())
        tag.SetPlotOutline(epgItem["plot"].GetString());
      if (epgItem.HasMember("description") && epgItem["description"].IsString())
        tag.SetPlot(epgItem["description"].GetString());
      if (epgItem.HasMember("icon") && epgItem["icon"].IsString())
        tag.SetIconPath(epgItem["icon"].GetString());
      if (epgItem.HasMember("genre") && epgItem["genre"].IsInt())
        tag.SetGenreType(epgItem["genre"].GetInt());
      if (epgItem.HasMember("episode_number") && epgItem["episode_number"].IsInt())
        tag.SetEpisodeNumber(epgItem["episode_number"].GetInt());
      if (epgItem.HasMember("season_number") && epgItem["season_number"].IsInt())
        tag.SetSeriesNumber(epgItem["season_number"].GetInt());
      if (epgItem.HasMember("episode_name") && epgItem["episode_name"].IsString())
        tag.SetEpisodeName(epgItem["episode_name"].GetString());

      results.Add(tag);
    }
  }
  return true;
}

bool EPGManager::IsEPGTagRecordable(const kodi::addon::PVREPGTag& tag, bool& isRecordable) {
  isRecordable = true;
  return true;
}

bool EPGManager::IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& isPlayable,
                                  const std::function<bool(int, std::string&, std::string&, int&)>& getChannelInfo) {
  isPlayable = false;
  int channelUid = tag.GetUniqueChannelId();
  std::string provider, channelId;
  int catchupHours = 0;

  if (!getChannelInfo(channelUid, provider, channelId, catchupHours)) return false;
  if (catchupHours <= 0) return false;

  time_t now = std::time(nullptr);
  time_t startTime = tag.GetStartTime();
  time_t endTime = tag.GetEndTime();

  if (startTime > now) return false;

  time_t catchupStart = now - (catchupHours * 3600);
  if (startTime < catchupStart) return false;

  isPlayable = true;
  return true;
}

bool EPGManager::GetEPGTagStreamProperties(const kodi::addon::PVREPGTag& tag,
                                           std::vector<kodi::addon::PVRStreamProperty>& properties,
                                           const std::function<std::string(const std::string&)>& httpGet,
                                           const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson,
                                           const std::function<bool(int, std::string&, std::string&, int&)>& getChannelInfo,
                                           const std::function<bool(int, UltimateChannel&)>& getChannelByUid,
                                           const std::function<bool()>& isBackendAvailable,
                                           const std::function<bool(const std::string&)>& retryBackendCall) {
  int channelUid = tag.GetUniqueChannelId();
  unsigned int broadcastId = tag.GetUniqueBroadcastId();
  std::string provider, channelId;
  int catchupHours = 0;

  if (!getChannelInfo(channelUid, provider, channelId, catchupHours)) return false;
  if (catchupHours <= 0) return false;

  if (!isBackendAvailable() && !retryBackendCall("EPG stream playback")) return false;

  std::ostringstream streamApiUrl;
  streamApiUrl << "/api/providers/" << provider
               << "/channels/" << channelId << "/stream"
               << "?start_time=" << tag.GetStartTime()
               << "&end_time=" << tag.GetEndTime()
               << "&epg_id=" << broadcastId;

  bool useCdm = false;
  UltimateChannel channel;
  if (getChannelByUid(channelUid, channel)) {
    useCdm = channel.useCdm;
    if (!channel.country.empty()) streamApiUrl << "&country=" << channel.country;
  }

  std::string response = httpGet(streamApiUrl.str());
  if (response.empty()) return false;

  rapidjson::Document document;
  std::string manifestUrl;
  if (parseJson(response, document) && document.IsObject()) {
    if (document.HasMember("manifest_url") && document["manifest_url"].IsString()) {
      manifestUrl = document["manifest_url"].GetString();
    } else return false;
  } else {
    manifestUrl = streamApiUrl.str();
  }

  properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.adaptive");
  properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, manifestUrl);
  properties.emplace_back("inputstream.adaptive.play_timeshift_buffer", "false");

  // DRM properties need to be applied by caller
  // This will be handled by PVRUltimate::ApplyDRMProperties

  return true;
}