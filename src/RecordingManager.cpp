#include "RecordingManager.h"
#include "Utils.h"
#include <algorithm>

const std::set<std::string> RecordingManager::PLAYABLE_STATUSES = {"COMPLETED", "RECORDING"};

bool RecordingManager::LoadRecordings(const std::vector<UltimateProvider>& providers,
                                      const std::function<std::string(const std::string&)>& httpGet,
                                      const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson) {
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
                                                 const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson,
                                                 std::vector<UltimateRecording>& outRecordings) {
  std::string response = httpGet("/api/providers/" + provider + "/recordings");
  if (response.empty()) return;

  rapidjson::Document document;
  if (!parseJson(response, document)) return;
  if (!document.HasMember("recordings") || !document["recordings"].IsArray()) return;

  for (const auto& recJson : document["recordings"].GetArray()) {
    UltimateRecording rec;
    rec.provider = provider;

    if (recJson.HasMember("Id") && recJson["Id"].IsString())
      rec.uniqueId = recJson["Id"].GetString();
    else continue;

    rec.title = (recJson.HasMember("Name") && recJson["Name"].IsString()) ? recJson["Name"].GetString() : rec.uniqueId;
    rec.channelName = (recJson.HasMember("ChannelName") && recJson["ChannelName"].IsString()) ? recJson["ChannelName"].GetString() : "";
    rec.channelUid = (recJson.HasMember("ChannelUid") && recJson["ChannelUid"].IsInt()) ? recJson["ChannelUid"].GetInt() : 0;
    rec.isRadio = (recJson.HasMember("ChannelType") && recJson["ChannelType"].IsString() && std::string(recJson["ChannelType"].GetString()) == "RADIO");

    rec.startTime = (recJson.HasMember("RecordingTime") && recJson["RecordingTime"].IsString()) ? Utils::ParseISO8601(recJson["RecordingTime"].GetString()) : 0;
    rec.durationSeconds = (recJson.HasMember("DurationSeconds") && recJson["DurationSeconds"].IsInt()) ? recJson["DurationSeconds"].GetInt() : 0;
    rec.endTime = rec.startTime + rec.durationSeconds;
    rec.firstAired = (recJson.HasMember("FirstAired") && recJson["FirstAired"].IsString()) ? recJson["FirstAired"].GetString() : "";

    rec.seasonNumber = (recJson.HasMember("SeasonNumber") && recJson["SeasonNumber"].IsInt()) ? recJson["SeasonNumber"].GetInt() : 0;
    rec.episodeNumber = (recJson.HasMember("EpisodeNumber") && recJson["EpisodeNumber"].IsInt()) ? recJson["EpisodeNumber"].GetInt() : 0;
    rec.episodeName = (recJson.HasMember("EpisodeName") && recJson["EpisodeName"].IsString()) ? recJson["EpisodeName"].GetString() : "";
    rec.seriesTitle = (recJson.HasMember("SeriesTitle") && recJson["SeriesTitle"].IsString()) ? recJson["SeriesTitle"].GetString() : "";
    rec.seriesId = (recJson.HasMember("SeriesId") && recJson["SeriesId"].IsString()) ? recJson["SeriesId"].GetString() : "";

    rec.plot = (recJson.HasMember("Plot") && recJson["Plot"].IsString()) ? recJson["Plot"].GetString() : "";
    rec.plotOutline = (recJson.HasMember("PlotOutline") && recJson["PlotOutline"].IsString()) ? recJson["PlotOutline"].GetString() : "";
    rec.genreDescription = (recJson.HasMember("GenreDescription") && recJson["GenreDescription"].IsString()) ? recJson["GenreDescription"].GetString() : "";
    rec.genreType = (recJson.HasMember("GenreType") && recJson["GenreType"].IsInt()) ? recJson["GenreType"].GetInt() : 0;
    rec.genreSubType = (recJson.HasMember("GenreSubType") && recJson["GenreSubType"].IsInt()) ? recJson["GenreSubType"].GetInt() : 0;

    rec.iconPath = (recJson.HasMember("IconPath") && recJson["IconPath"].IsString()) ? recJson["IconPath"].GetString() : "";
    rec.thumbnailUrl = (recJson.HasMember("ThumbnailUrl") && recJson["ThumbnailUrl"].IsString()) ? recJson["ThumbnailUrl"].GetString() : "";
    rec.fanartUrl = (recJson.HasMember("FanartUrl") && recJson["FanartUrl"].IsString()) ? recJson["FanartUrl"].GetString() : "";

    rec.playCount = (recJson.HasMember("PlayCount") && recJson["PlayCount"].IsInt()) ? recJson["PlayCount"].GetInt() : 0;
    rec.lastPlayedPosition = (recJson.HasMember("LastPlayedPosition") && recJson["LastPlayedPosition"].IsInt()) ? recJson["LastPlayedPosition"].GetInt() : 0;

    rec.directory = (recJson.HasMember("Directory") && recJson["Directory"].IsString()) ? recJson["Directory"].GetString() : "";
    rec.sizeInBytes = (recJson.HasMember("SizeInBytes") && recJson["SizeInBytes"].IsInt()) ? recJson["SizeInBytes"].GetInt() : 0;
    rec.priority = (recJson.HasMember("Priority") && recJson["Priority"].IsInt()) ? recJson["Priority"].GetInt() : 0;
    rec.lifetime = (recJson.HasMember("Lifetime") && recJson["Lifetime"].IsInt()) ? recJson["Lifetime"].GetInt() : 0;
    rec.flags = (recJson.HasMember("Flags") && recJson["Flags"].IsString()) ? recJson["Flags"].GetString() : "";
    rec.clientProviderUid = (recJson.HasMember("ClientProviderUid") && recJson["ClientProviderUid"].IsInt()) ? recJson["ClientProviderUid"].GetInt() : 0;
    rec.providerName = (recJson.HasMember("ProviderName") && recJson["ProviderName"].IsString()) ? recJson["ProviderName"].GetString() : "";

    rec.epgEventId = (recJson.HasMember("EpgEventId") && recJson["EpgEventId"].IsInt()) ? recJson["EpgEventId"].GetInt() : 0;
    rec.releaseYear = (recJson.HasMember("ReleaseYear") && recJson["ReleaseYear"].IsInt()) ? recJson["ReleaseYear"].GetInt() : 0;

    rec.status = (recJson.HasMember("Status") && recJson["Status"].IsString()) ? recJson["Status"].GetString() : "";
    rec.isPlayable = (PLAYABLE_STATUSES.find(rec.status) != PLAYABLE_STATUSES.end());
    rec.isDeleted = (recJson.HasMember("IsDeleted") && recJson["IsDeleted"].IsBool()) ? recJson["IsDeleted"].GetBool() : false;

    outRecordings.push_back(rec);
  }
}

int RecordingManager::GetRecordingsAmount(bool deleted) const {
  std::shared_lock<std::shared_mutex> lock(m_dataMutex);
  return std::ranges::count_if(m_recordings,
                               [deleted](const UltimateRecording& r){ return r.isDeleted == deleted; });
}

bool RecordingManager::GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results) {
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
  if (!recording.seriesTitle.empty()) kodiRecording.SetSeriesTitle(recording.seriesTitle);

  if (recording.releaseYear > 0) kodiRecording.SetYear(recording.releaseYear);
  if (!recording.firstAired.empty()) kodiRecording.SetFirstAired(recording.firstAired);
  if (recording.epgEventId > 0) kodiRecording.SetEPGEventId(static_cast<unsigned int>(recording.epgEventId));

  if (!recording.directory.empty()) kodiRecording.SetDirectory(recording.directory);
  if (recording.sizeInBytes > 0) kodiRecording.SetSizeInBytes(recording.sizeInBytes);
  if (recording.priority > 0) kodiRecording.SetPriority(recording.priority);
  if (recording.lifetime > 0) kodiRecording.SetLifetime(recording.lifetime);
  if (!recording.flags.empty()) kodiRecording.SetFlags(recording.flags);
  if (recording.clientProviderUid > 0) kodiRecording.SetClientProviderUid(recording.clientProviderUid);
  if (!recording.providerName.empty()) kodiRecording.SetProviderName(recording.providerName);

  kodiRecording.SetDeleted(recording.isDeleted);

  PVR_RECORDING_STATUS kodiStatus = PVR_RECORDING_STATUS_UNKNOWN;
  if (recording.status == "PENDING") kodiStatus = PVR_RECORDING_STATUS_SCHEDULED;
  else if (recording.status == "RECORDING") kodiStatus = PVR_RECORDING_STATUS_RECORDING;
  else if (recording.status == "COMPLETED") kodiStatus = PVR_RECORDING_STATUS_COMPLETED;
  else if (recording.status == "FAILED") kodiStatus = PVR_RECORDING_STATUS_FAILED;
  else if (recording.status == "CANCELLED") kodiStatus = PVR_RECORDING_STATUS_CANCELLED;
  else if (recording.status == "DELETED") kodiStatus = PVR_RECORDING_STATUS_DELETED;
  kodiRecording.SetStatus(kodiStatus);

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

  if (!httpDelete(buildApiUrl("/api/providers/" + provider + "/recordings/" + recordingId))) {
    return false;
  }

  {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    UltimateRecording* rec = FindRecording(recordingId);
    if (rec) {
      rec->isDeleted = true;
    }
  }

  return true;
}

bool RecordingManager::GetRecordingStreamProperties(const std::string& recordingId,
                                                    std::vector<kodi::addon::PVRStreamProperty>& properties,
                                                    const std::function<std::string(const std::string&)>& buildApiUrl,
                                                    const std::function<std::string(const std::string&)>& httpGet,
                                                    const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson,
                                                    const std::function<bool(const std::string&, std::string&, std::string&, std::string&)>& httpGetWithHeaders,
                                                    bool supportsPiggyback) {
  std::string provider, uniqueId;
  
  {
    std::shared_lock<std::shared_mutex> lock(m_dataMutex);
    UltimateRecording* rec = FindRecording(recordingId);
    if (!rec || !rec->isPlayable) return false;
    provider = rec->provider;
    uniqueId = rec->uniqueId;
  }

  std::string manifestUrl = buildApiUrl("/api/providers/" + provider + "/recordings/" + uniqueId + "/manifest");
  std::string response, drmConfigsBase64, streamHeadersBase64;

  if (supportsPiggyback) {
    if (!httpGetWithHeaders(manifestUrl, response, drmConfigsBase64, streamHeadersBase64)) return false;
  } else {
    response = httpGet(manifestUrl);
    if (response.empty()) return false;
  }

  rapidjson::Document document;
  if (!parseJson(response, document)) return false;

  std::string streamUrl;
  if (document.HasMember("manifest_url") && document["manifest_url"].IsString()) {
    streamUrl = document["manifest_url"].GetString();
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