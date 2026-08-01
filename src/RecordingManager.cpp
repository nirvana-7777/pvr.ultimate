#include "RecordingManager.h"
#include "Utils.h"
#include <kodi/General.h>
#include <algorithm>

const std::set<std::string> RecordingManager::PLAYABLE_STATUSES = {"COMPLETED", "RECORDING"};

bool RecordingManager::LoadRecordings(const std::vector<UltimateProvider>& providers,
                                      const std::function<std::string(const std::string&)>& httpGet,
                                      const std::function<bool(const std::string&, nlohmann::json&)>& parseJson) {
  std::vector<UltimateRecording> newRecordings;

  for (const auto& provider : providers) {
    if (provider.enabled) {
      LoadRecordingsForProvider(provider.name, httpGet, parseJson, newRecordings);
    }
  }

  std::unique_lock<std::shared_mutex> lock(m_dataMutex);
  m_recordings = std::move(newRecordings);

  return true;
}

void RecordingManager::LoadRecordingsForProvider(const std::string& provider,
                                                 const std::function<std::string(const std::string&)>& httpGet,
                                                 const std::function<bool(const std::string&, nlohmann::json&)>& parseJson,
                                                 std::vector<UltimateRecording>& outRecordings) {
  std::string url = "/api/providers/" + Utils::UrlPathEncode(provider) + "/recordings";
  std::string response = httpGet(url);
  if (response.empty()) {
    kodi::Log(ADDON_LOG_WARNING, "Empty response from %s", Utils::RedactUrl(url).c_str());
    return;
  }

  nlohmann::json document;
  if (!parseJson(response, document)) return;
  if (!document.contains("recordings") || !document["recordings"].is_array()) return;

  for (const auto& recJson : document["recordings"]) {
    UltimateRecording rec;
    rec.provider = provider;

    if (recJson.contains("Id") && recJson["Id"].is_string())
      rec.uniqueId = recJson["Id"].get<std::string>();
    else continue;

    rec.title = (recJson.contains("Name") && recJson["Name"].is_string()) ? recJson["Name"].get<std::string>() : rec.uniqueId;
    rec.channelName = (recJson.contains("ChannelName") && recJson["ChannelName"].is_string()) ? recJson["ChannelName"].get<std::string>() : "";
    rec.channelUid = (recJson.contains("ChannelUid") && recJson["ChannelUid"].is_number_integer()) ? recJson["ChannelUid"].get<int>() : 0;
    rec.isRadio = (recJson.contains("ChannelType") && recJson["ChannelType"].is_string() && recJson["ChannelType"].get<std::string>() == "RADIO");

    rec.startTime = (recJson.contains("RecordingTime") && recJson["RecordingTime"].is_string()) ? Utils::ParseISO8601(recJson["RecordingTime"].get<std::string>()) : 0;
    rec.durationSeconds = (recJson.contains("DurationSeconds") && recJson["DurationSeconds"].is_number_integer()) ? recJson["DurationSeconds"].get<int>() : 0;
    rec.endTime = rec.startTime + rec.durationSeconds;
    rec.firstAired = (recJson.contains("FirstAired") && recJson["FirstAired"].is_string()) ? recJson["FirstAired"].get<std::string>() : "";

    rec.seasonNumber = (recJson.contains("SeasonNumber") && recJson["SeasonNumber"].is_number_integer()) ? recJson["SeasonNumber"].get<int>() : 0;
    rec.episodeNumber = (recJson.contains("EpisodeNumber") && recJson["EpisodeNumber"].is_number_integer()) ? recJson["EpisodeNumber"].get<int>() : 0;
    rec.episodeName = (recJson.contains("EpisodeName") && recJson["EpisodeName"].is_string()) ? recJson["EpisodeName"].get<std::string>() : "";
    rec.seriesTitle = (recJson.contains("SeriesTitle") && recJson["SeriesTitle"].is_string()) ? recJson["SeriesTitle"].get<std::string>() : "";
    rec.seriesId = (recJson.contains("SeriesId") && recJson["SeriesId"].is_string()) ? recJson["SeriesId"].get<std::string>() : "";

    rec.plot = (recJson.contains("Plot") && recJson["Plot"].is_string()) ? recJson["Plot"].get<std::string>() : "";
    rec.plotOutline = (recJson.contains("PlotOutline") && recJson["PlotOutline"].is_string()) ? recJson["PlotOutline"].get<std::string>() : "";
    rec.genreDescription = (recJson.contains("GenreDescription") && recJson["GenreDescription"].is_string()) ? recJson["GenreDescription"].get<std::string>() : "";
    rec.genreType = (recJson.contains("GenreType") && recJson["GenreType"].is_number_integer()) ? recJson["GenreType"].get<int>() : 0;
    rec.genreSubType = (recJson.contains("GenreSubType") && recJson["GenreSubType"].is_number_integer()) ? recJson["GenreSubType"].get<int>() : 0;

    rec.iconPath = (recJson.contains("IconPath") && recJson["IconPath"].is_string()) ? recJson["IconPath"].get<std::string>() : "";
    rec.thumbnailUrl = (recJson.contains("ThumbnailUrl") && recJson["ThumbnailUrl"].is_string()) ? recJson["ThumbnailUrl"].get<std::string>() : "";
    rec.fanartUrl = (recJson.contains("FanartUrl") && recJson["FanartUrl"].is_string()) ? recJson["FanartUrl"].get<std::string>() : "";

    rec.playCount = (recJson.contains("PlayCount") && recJson["PlayCount"].is_number_integer()) ? recJson["PlayCount"].get<int>() : 0;
    rec.lastPlayedPosition = (recJson.contains("LastPlayedPosition") && recJson["LastPlayedPosition"].is_number_integer()) ? recJson["LastPlayedPosition"].get<int>() : 0;

    rec.directory = (recJson.contains("Directory") && recJson["Directory"].is_string()) ? recJson["Directory"].get<std::string>() : "";
    rec.sizeInBytes = (recJson.contains("SizeInBytes") && recJson["SizeInBytes"].is_number_integer()) ? recJson["SizeInBytes"].get<int>() : 0;
    rec.priority = (recJson.contains("Priority") && recJson["Priority"].is_number_integer()) ? recJson["Priority"].get<int>() : 0;
    rec.lifetime = (recJson.contains("Lifetime") && recJson["Lifetime"].is_number_integer()) ? recJson["Lifetime"].get<int>() : 0;
    rec.flags = (recJson.contains("Flags") && recJson["Flags"].is_string()) ? recJson["Flags"].get<std::string>() : "";
    rec.clientProviderUid = (recJson.contains("ClientProviderUid") && recJson["ClientProviderUid"].is_number_integer()) ? recJson["ClientProviderUid"].get<int>() : 0;
    rec.providerName = (recJson.contains("ProviderName") && recJson["ProviderName"].is_string()) ? recJson["ProviderName"].get<std::string>() : "";

    rec.epgEventId = (recJson.contains("EpgEventId") && recJson["EpgEventId"].is_number_integer()) ? recJson["EpgEventId"].get<int>() : 0;
    rec.releaseYear = (recJson.contains("ReleaseYear") && recJson["ReleaseYear"].is_number_integer()) ? recJson["ReleaseYear"].get<int>() : 0;

    rec.status = (recJson.contains("Status") && recJson["Status"].is_string()) ? recJson["Status"].get<std::string>() : "";
    rec.isPlayable = (PLAYABLE_STATUSES.contains(rec.status));
    rec.isDeleted = (recJson.contains("IsDeleted") && recJson["IsDeleted"].is_boolean()) ? recJson["IsDeleted"].get<bool>() : false;

    outRecordings.push_back(rec);
  }
}

int RecordingManager::GetRecordingsAmount(bool deleted) const {
  std::shared_lock<std::shared_mutex> lock(m_dataMutex);
  return std::ranges::count_if(m_recordings,
                               [deleted](const UltimateRecording& r){ return r.isDeleted == deleted; });
}

bool RecordingManager::GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results) const {
  std::shared_lock<std::shared_mutex> lock(m_dataMutex);
  for (const auto& recording : m_recordings) {
    if (recording.isDeleted != deleted) continue;

    kodi::addon::PVRRecording kodiRecording;
    MapRecordingToKodi(recording, kodiRecording);
    results.Add(kodiRecording);
  }
  return true;
}

bool RecordingManager::MapRecordingToKodi(const UltimateRecording& recording,
                                          kodi::addon::PVRRecording& kodiRecording) {
  kodiRecording.SetRecordingId(recording.uniqueId);
  kodiRecording.SetTitle(recording.title);
  kodiRecording.SetChannelName(recording.channelName.empty() ? recording.provider : recording.channelName);
  if (recording.channelUid > 0) kodiRecording.SetChannelUid(recording.channelUid);
  kodiRecording.SetChannelType(recording.isRadio ? PVR_RECORDING_CHANNEL_TYPE_RADIO : PVR_RECORDING_CHANNEL_TYPE_TV);
  kodiRecording.SetRecordingTime(recording.startTime);
  kodiRecording.SetDuration(recording.durationSeconds);
  kodiRecording.SetPlayCount(recording.playCount);
  if (recording.lastPlayedPosition > 0) kodiRecording.SetLastPlayedPosition(recording.lastPlayedPosition);

  if (!recording.iconPath.empty()) kodiRecording.SetIconPath(recording.iconPath);
  if (!recording.thumbnailUrl.empty()) kodiRecording.SetThumbnailPath(recording.thumbnailUrl);
  if (!recording.fanartUrl.empty()) kodiRecording.SetFanartPath(recording.fanartUrl);

  if (!recording.plot.empty()) kodiRecording.SetPlot(recording.plot);
  if (!recording.plotOutline.empty()) kodiRecording.SetPlotOutline(recording.plotOutline);
  if (!recording.genreDescription.empty()) kodiRecording.SetGenreDescription(recording.genreDescription);
  if (recording.genreType > 0) kodiRecording.SetGenreType(recording.genreType);
  if (recording.genreSubType > 0) kodiRecording.SetGenreSubType(recording.genreSubType);

  if (recording.seasonNumber > 0) kodiRecording.SetSeriesNumber(recording.seasonNumber);
  if (recording.episodeNumber > 0) kodiRecording.SetEpisodeNumber(recording.episodeNumber);
  if (!recording.episodeName.empty()) kodiRecording.SetEpisodeName(recording.episodeName);
  // Note: kodi::addon::PVRRecording has no series-title field (only
  // Title/TitleExtraInfo/EpisodeName/SeriesNumber/EpisodeNumber), so
  // recording.seriesTitle is intentionally not sent to Kodi here.

  if (recording.releaseYear > 0) kodiRecording.SetYear(recording.releaseYear);
  if (!recording.firstAired.empty()) kodiRecording.SetFirstAired(recording.firstAired);
  if (recording.epgEventId > 0) kodiRecording.SetEPGEventId(static_cast<unsigned int>(recording.epgEventId));

  if (!recording.directory.empty()) kodiRecording.SetDirectory(recording.directory);
  if (recording.sizeInBytes > 0) kodiRecording.SetSizeInBytes(recording.sizeInBytes);
  if (recording.priority > 0) kodiRecording.SetPriority(recording.priority);
  if (recording.lifetime > 0) kodiRecording.SetLifetime(recording.lifetime);
  // TODO: kodi::addon::PVRRecording::SetFlags() takes an unsigned int bitmask
  // of PVR_RECORDING_FLAG_IS_SERIES/IS_NEW/IS_PREMIERE/IS_FINALE/IS_LIVE, but
  // recording.flags is the raw "Flags" string from the backend response. Need
  // to know the backend's actual string format before this can be translated
  // into the real bitmask -- left unset for now rather than guessing.
  if (recording.clientProviderUid > 0) kodiRecording.SetClientProviderUid(recording.clientProviderUid);
  if (!recording.providerName.empty()) kodiRecording.SetProviderName(recording.providerName);

  kodiRecording.SetIsDeleted(recording.isDeleted);

  // Note: kodi::addon::PVRRecording has no status field / PVR_RECORDING_STATUS
  // type in this API -- the only lifecycle bit Kodi exposes for a recording is
  // IsDeleted (set above). "PENDING"/"RECORDING" states from the backend are
  // presumably represented as Timers (see TimerManager::MapTimerStateToKodi),
  // not as a status on the recording itself; recording.status is still used
  // internally (see PLAYABLE_STATUSES / isPlayable above).

  return true;
}

bool RecordingManager::DeleteRecording(const std::string& recordingId,
                                       const std::function<std::string(const std::string&)>& buildApiUrl,
                                       const std::function<bool(const std::string&)>& httpDelete) {
  std::string provider;
  bool found = false;

  {
    std::shared_lock<std::shared_mutex> lock(m_dataMutex);
    UltimateRecording* rec = FindRecording(recordingId);
    if (!rec) return false;
    provider = rec->provider;
    found = true;
  }

  if (!found) return false;

  if (!httpDelete(buildApiUrl("/api/providers/" + Utils::UrlPathEncode(provider) +
                              "/recordings/" + Utils::UrlPathEncode(recordingId)))) {
    return false;
  }

  {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    if (UltimateRecording* rec = FindRecording(recordingId)) {
      rec->isDeleted = true;
    }
  }

  return true;
}

bool RecordingManager::GetRecordingStreamProperties(const std::string& recordingId,
                                                    std::vector<kodi::addon::PVRStreamProperty>& properties,
                                                    const std::function<std::string(const std::string&)>& buildApiUrl,
                                                    const std::function<std::string(const std::string&)>& httpGet,
                                                    const std::function<bool(const std::string&, nlohmann::json&)>& parseJson,
                                                    const std::function<bool(const std::string&, std::string&, std::string&, std::string&)>& httpGetWithHeaders,
                                                    bool supportsPiggyback,
                                                    std::string& drmConfigsBase64,
                                                    std::string& streamHeadersBase64) {
  std::string provider, uniqueId;
  drmConfigsBase64.clear();
  streamHeadersBase64.clear();

  {
    std::shared_lock<std::shared_mutex> lock(m_dataMutex);
    UltimateRecording* rec = FindRecording(recordingId);
    if (!rec || !rec->isPlayable) return false;
    provider = rec->provider;
    uniqueId = rec->uniqueId;
  }

  std::string manifestUrl = buildApiUrl("/api/providers/" + Utils::UrlPathEncode(provider) +
                                        "/recordings/" + Utils::UrlPathEncode(uniqueId) + "/manifest");
  std::string response;

  if (supportsPiggyback) {
    if (!httpGetWithHeaders(manifestUrl, response, drmConfigsBase64, streamHeadersBase64)) return false;
  } else {
    response = httpGet(manifestUrl);
    if (response.empty()) return false;
  }

  nlohmann::json document;
  if (!parseJson(response, document)) return false;

  std::string streamUrl;
  if (document.contains("manifest_url") && document["manifest_url"].is_string()) {
    streamUrl = document["manifest_url"].get<std::string>();
  } else return false;

  properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.adaptive");
  properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, streamUrl);

  // DRM properties need to be applied by caller
  // This will be handled by PVRUltimate::ApplyDRMProperties

  return true;
}

bool RecordingManager::GetRecordingEdl(const std::string& recordingId, std::vector<kodi::addon::PVREDLEntry>& edl) {
  return false;
}

UltimateRecording* RecordingManager::FindRecording(const std::string& recordingId) {
  for (auto& rec : m_recordings) {
    if (rec.uniqueId == recordingId) return &rec;
  }
  return nullptr;
}