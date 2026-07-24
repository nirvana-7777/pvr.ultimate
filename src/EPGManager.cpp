#include "EPGManager.h"
#include <kodi/General.h>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <vector>

// Shared parsing logic - single source of truth for EPG JSON -> PVREPGTag mapping.
bool EPGManager::ParseEPGResponse(const std::string& response,
                                   int channelUid,
                                   const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson,
                                   kodi::addon::PVREPGTagsResultSet& results) {
  rapidjson::Document document;
  if (!parseJson(response, document)) return false;

  if (!document.HasMember("epg") || !document["epg"].IsArray()) return false;

  struct ParsedTag {
    kodi::addon::PVREPGTag tag;
    uint64_t start;
    uint64_t end;
    std::string title;
  };
  std::vector<ParsedTag> parsedTags;

  for (const auto& epgItem : document["epg"].GetArray()) {
    if (!epgItem.IsObject()) continue;

    uint64_t eStart = (epgItem.HasMember("start") && epgItem["start"].IsUint64()) ? epgItem["start"].GetUint64() : 0;
    uint64_t eEnd = (epgItem.HasMember("end") && epgItem["end"].IsUint64()) ? epgItem["end"].GetUint64() : 0;

    // Backend occasionally sends malformed entries with zero or inverted timestamps
    // (observed in live responses) - these are unusable for Kodi's EPG grid, skip them.
    if (eStart == 0 || eEnd == 0 || eEnd <= eStart) continue;

    kodi::addon::PVREPGTag tag;
    unsigned int broadcastId = static_cast<unsigned int>(channelUid ^ (eStart << 16) ^ (eStart >> 16) ^ eEnd);

    tag.SetUniqueBroadcastId(broadcastId);
    tag.SetUniqueChannelId(channelUid);
    tag.SetStartTime(static_cast<time_t>(eStart));
    tag.SetEndTime(static_cast<time_t>(eEnd));

    std::string title;
    if (epgItem.HasMember("title") && epgItem["title"].IsString())
      title = epgItem["title"].GetString();
    if (!title.empty())
      tag.SetTitle(title);

    // NOTE: verified against live curl output (magentaeu_at, 2026-07-24) that this
    // endpoint only ever sends a single "plot" field - there is no separate
    // "description" field. The previous mapping (plot -> SetPlotOutline,
    // description -> SetPlot) meant SetPlot() was never actually called, so Kodi's
    // Info dialog synopsis was silently empty for every program. We now map "plot"
    // to both SetPlot (Info dialog) and SetPlotOutline (EPG grid blurb).
    // If the backend ever starts sending a distinct "description" field for the
    // full synopsis, split this back into two mappings.
    if (epgItem.HasMember("plot") && epgItem["plot"].IsString()) {
      std::string plot = epgItem["plot"].GetString();
      if (!plot.empty()) {
        tag.SetPlot(plot);
        tag.SetPlotOutline(plot);
      }
    }

    if (epgItem.HasMember("icon") && epgItem["icon"].IsString()) {
      std::string icon = epgItem["icon"].GetString();
      if (!icon.empty())
        tag.SetIconPath(icon);
    }

    if (epgItem.HasMember("genre") && epgItem["genre"].IsInt())
      tag.SetGenreType(epgItem["genre"].GetInt());

    // Only set season/episode numbers if they're valid (greater than 0)
    if (epgItem.HasMember("season_number") && epgItem["season_number"].IsInt()) {
      int seasonNum = epgItem["season_number"].GetInt();
      if (seasonNum > 0)
        tag.SetSeriesNumber(seasonNum);
    }
    if (epgItem.HasMember("episode_number") && epgItem["episode_number"].IsInt()) {
      int episodeNum = epgItem["episode_number"].GetInt();
      if (episodeNum > 0)
        tag.SetEpisodeNumber(episodeNum);
    }

    if (epgItem.HasMember("episode_name") && epgItem["episode_name"].IsString()) {
      std::string epName = epgItem["episode_name"].GetString();
      if (!epName.empty())
        tag.SetEpisodeName(epName);
    }

    parsedTags.push_back({std::move(tag), eStart, eEnd, std::move(title)});
  }

  // De-duplication.
  //
  // IMPORTANT - this is not a simple "identical entry appears twice" bug. Live curl
  // output for magentaeu_at/206963752351 shows the backend interleaving what look
  // like two complete, internally self-consistent schedules for the same day,
  // offset from each other by a few minutes per program (e.g. "Dennstein & Schwarz -
  // Rufmord" appears as [09:38-11:06] and again as [09:45-11:15], both times paired
  // with programs before/after that are themselves contiguous within their own
  // chain). No two entries are byte-identical, so a plain sort+unique on
  // start/end would not catch this - it has to be done via overlap detection.
  //
  // We do NOT know which of the two schedule versions is authoritative - that's a
  // backend/provider question, not something to silently resolve on the client.
  // Below, when a same-titled entry overlaps with the previously *kept* entry, we
  // drop it and log a WARNING (rather than picking silently) so this is visible in
  // logs. Default behavior keeps whichever version starts earlier after sorting -
  // that is a reasonable default, not a verified-correct choice.
  std::sort(parsedTags.begin(), parsedTags.end(),
            [](const ParsedTag& a, const ParsedTag& b) { return a.start < b.start; });

  uint64_t lastAcceptedEnd = 0;
  std::string lastAcceptedTitle;
  bool haveAccepted = false;

  for (auto& pt : parsedTags) {
    if (haveAccepted && !pt.title.empty() && pt.title == lastAcceptedTitle &&
        pt.start < lastAcceptedEnd) {
      kodi::Log(ADDON_LOG_WARNING,
                "EPG dedup: dropping overlapping duplicate '%s' start=%llu end=%llu on "
                "channel %d (kept earlier version ending at %llu) - two schedule "
                "versions may be present in the feed, verify with backend which is "
                "correct",
                pt.title.c_str(), (unsigned long long)pt.start, (unsigned long long)pt.end,
                channelUid, (unsigned long long)lastAcceptedEnd);
      continue;
    }

    results.Add(pt.tag);
    lastAcceptedEnd = pt.end;
    lastAcceptedTitle = pt.title;
    haveAccepted = true;
  }

  return true;
}

// Original signature - preserved for any other call sites, delegates to extended version.
bool EPGManager::GetEPGForChannel(int channelUid, time_t start, time_t end,
                                  const std::function<std::string(const std::string&)>& httpGet,
                                  const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson,
                                  const std::function<bool(int, UltimateChannel&)>& getChannelByUid,
                                  kodi::addon::PVREPGTagsResultSet& results) {
  return GetEPGForChannel(channelUid, start, end, httpGet, parseJson, getChannelByUid,
                          results, nullptr, false);
}

// Extended signature with optional database-EPG-service support.
//
// Contract for httpGetAbsolute: it receives a path that already starts with "/" and already
// includes any versioning prefix (e.g. "/api/v1/providers/..."), and is expected to prepend
// only scheme+host (m_epgServiceUrl) before making the request. It must NOT itself try to
// detect/insert a version prefix - that logic belongs here, in one place, not split across
// two layers (that split is what caused the double "/api/v1/api/v1/..." bug in an earlier draft).
bool EPGManager::GetEPGForChannel(int channelUid, time_t start, time_t end,
                                  const std::function<std::string(const std::string&)>& httpGet,
                                  const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson,
                                  const std::function<bool(int, UltimateChannel&)>& getChannelByUid,
                                  kodi::addon::PVREPGTagsResultSet& results,
                                  const std::function<std::string(const std::string&)>& httpGetAbsolute,
                                  bool useDatabaseEpg) {
  std::string provider, channelId, country;
  UltimateChannel channel;

  if (!getChannelByUid(channelUid, channel)) return false;
  provider = channel.provider;
  channelId = channel.channelId;
  country = channel.country;

  if (useDatabaseEpg && httpGetAbsolute) {
    std::ostringstream dbUrl;
    dbUrl << "/api/v1/providers/" << provider << "/channels/" << channelId << "/epg"
          << "?start_time=" << start << "&end_time=" << end;

    // Single call - httpGetAbsolute performs the request and returns the response body directly.
    std::string dbResponse = httpGetAbsolute(dbUrl.str());

    if (!dbResponse.empty()) {
      if (ParseEPGResponse(dbResponse, channelUid, parseJson, results)) {
        return true;
      }
      kodi::Log(ADDON_LOG_WARNING, "Database EPG parse failed for channel %d, falling back to backend", channelUid);
    } else {
      kodi::Log(ADDON_LOG_WARNING, "Database EPG empty response for channel %d, falling back to backend", channelUid);
    }
    // Falls through to the backend API path below.
  }

  std::ostringstream url;
  url << "/api/providers/" << provider << "/channels/" << channelId << "/epg"
      << "?start_time=" << start << "&end_time=" << end;
  if (!country.empty()) url << "&country=" << country;

  std::string response = httpGet(url.str());
  if (response.empty()) return false;

  return ParseEPGResponse(response, channelUid, parseJson, results);
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
                                           const std::function<bool(const std::string&)>& retryBackendCall,
                                           const std::function<std::string(const std::string&, const std::string&)>& getManifestUrl,
                                           const std::function<bool(const std::string&, std::string&, std::string&, std::string&)>& httpGetWithHeaders,
                                           bool supportsPiggyback,
                                           std::string& drmConfigsBase64,
                                           std::string& streamHeadersBase64) {
  int channelUid = tag.GetUniqueChannelId();
  unsigned int broadcastId = tag.GetUniqueBroadcastId();
  std::string provider, channelId;
  int catchupHours = 0;

  drmConfigsBase64.clear();
  streamHeadersBase64.clear();

  if (!getChannelInfo(channelUid, provider, channelId, catchupHours)) return false;
  if (catchupHours <= 0) return false;

  UltimateChannel channel;
  if (!getChannelByUid(channelUid, channel)) return false;

  if (!isBackendAvailable() && !retryBackendCall("EPG stream playback")) return false;

  // Same manifest endpoint/contract as live playback - the backend, not the client,
  // decides the catchup URL shape via catchup_stream_url_template below.
  std::string manifestApiUrl = getManifestUrl(provider, channelId);

  std::string response;
  if (supportsPiggyback) {
    if (!httpGetWithHeaders(manifestApiUrl, response, drmConfigsBase64, streamHeadersBase64)) {
      return false;
    }
  } else {
    response = httpGet(manifestApiUrl);
    if (response.empty()) return false;
  }

  rapidjson::Document document;
  if (!parseJson(response, document) || !document.IsObject()) return false;

  if (!document.HasMember("catchup_stream_url_template") ||
      !document["catchup_stream_url_template"].IsString()) {
    kodi::Log(ADDON_LOG_WARNING,
              "No catchup_stream_url_template in manifest for %s/%s; channel reports "
              "catchup support (catchupHours=%d) but backend did not provide a template",
              provider.c_str(), channelId.c_str(), catchupHours);
    return false;
  }

  std::string streamUrl = document["catchup_stream_url_template"].GetString();

  auto hasPlaceholder = [&streamUrl](const std::string& placeholder) {
    return streamUrl.find(placeholder) != std::string::npos;
  };
  auto substitute = [&streamUrl](const std::string& placeholder, const std::string& value) {
    size_t pos = streamUrl.find(placeholder);
    if (pos != std::string::npos) streamUrl.replace(pos, placeholder.length(), value);
  };

  // start_time/end_time are mandatory - a template without them isn't usable for catchup.
  if (!hasPlaceholder("{start_time}") || !hasPlaceholder("{end_time}")) {
    kodi::Log(ADDON_LOG_WARNING,
              "catchup_stream_url_template for %s/%s missing {start_time}/{end_time} "
              "placeholder(s): %s",
              provider.c_str(), channelId.c_str(), streamUrl.c_str());
    return false;
  }

  // epg_id/country are only sent when the template asks for them. If the template requires
  // {country} but we have none for this channel, that's a data problem worth surfacing rather
  // than silently leaving the literal placeholder in the URL.
  if (hasPlaceholder("{country}") && channel.country.empty()) {
    kodi::Log(ADDON_LOG_WARNING,
              "catchup_stream_url_template for %s/%s requires {country} but channel has none set",
              provider.c_str(), channelId.c_str());
    return false;
  }

  substitute("{start_time}", std::to_string(tag.GetStartTime()));
  substitute("{end_time}", std::to_string(tag.GetEndTime()));
  substitute("{epg_id}", std::to_string(broadcastId));
  if (hasPlaceholder("{country}")) substitute("{country}", channel.country);

  properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.adaptive");
  properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, streamUrl);
  properties.emplace_back("inputstream.adaptive.play_timeshift_buffer", "false");

  // DRM (drmConfigsBase64) and stream headers (streamHeadersBase64) are applied by the caller
  // via ApplyDRMProperties/ApplyStreamHeaders, same as the live channel path.

  return true;
}