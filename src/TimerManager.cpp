#include "TimerManager.h"
#include "Utils.h"
#include <kodi/General.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

bool TimerManager::LoadTimerTypes(const std::vector<UltimateProvider>& providers,
                                  const std::function<std::string(const std::string&)>& httpGet,
                                  const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson) {
  std::vector<UltimateTimerType> newTimerTypes;

  for (const auto& provider : providers) {
    if (provider.enabled) {
      LoadTimerTypesForProvider(provider.name, httpGet, parseJson, newTimerTypes);
    }
  }

  if (newTimerTypes.empty()) {
    UltimateTimerType manual;
    manual.id = 1;
    manual.description = "Manual";
    manual.priority = 50;
    newTimerTypes.push_back(manual);
  }

  std::unique_lock<std::shared_mutex> lock(m_dataMutex);
  m_timerTypes = std::move(newTimerTypes);

  return true;
}

void TimerManager::LoadTimerTypesForProvider(const std::string& provider,
                                             const std::function<std::string(const std::string&)>& httpGet,
                                             const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson,
                                             std::vector<UltimateTimerType>& outTimerTypes) {
  std::string response = httpGet("/api/providers/" + provider + "/timer-types");
  if (response.empty()) return;

  rapidjson::Document document;
  if (!parseJson(response, document)) return;
  if (!document.HasMember("timer_types") || !document["timer_types"].IsArray()) return;

  for (const auto& ttJson : document["timer_types"].GetArray()) {
    UltimateTimerType timerType;
    if (ttJson.HasMember("id") && ttJson["id"].IsInt()) timerType.id = ttJson["id"].GetInt();
    else continue;
    timerType.description = (ttJson.HasMember("description") && ttJson["description"].IsString()) ? ttJson["description"].GetString() : "Timer";
    timerType.priority = (ttJson.HasMember("priority") && ttJson["priority"].IsInt()) ? ttJson["priority"].GetInt() : 50;
    outTimerTypes.push_back(timerType);
  }
}

bool TimerManager::LoadTimers(const std::vector<UltimateProvider>& providers,
                              const std::function<std::string(const std::string&)>& httpGet,
                              const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson) {
  std::vector<UltimateTimer> newTimers;

  for (const auto& provider : providers) {
    if (provider.enabled) {
      LoadTimersForProvider(provider.name, httpGet, parseJson, newTimers);
    }
  }

  std::unique_lock<std::shared_mutex> lock(m_dataMutex);
  m_timers = std::move(newTimers);

  return true;
}

void TimerManager::LoadTimersForProvider(const std::string& provider,
                                         const std::function<std::string(const std::string&)>& httpGet,
                                         const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson,
                                         std::vector<UltimateTimer>& outTimers) {
  std::string response = httpGet("/api/providers/" + provider + "/timers?include_inactive=true");
  if (response.empty()) return;

  rapidjson::Document document;
  if (!parseJson(response, document)) return;
  if (!document.HasMember("timers") || !document["timers"].IsArray()) return;

  for (const auto& timerJson : document["timers"].GetArray()) {
    UltimateTimer timer;
    timer.provider = provider;

    if (timerJson.HasMember("client_index") && timerJson["client_index"].IsInt())
      timer.clientIndex = timerJson["client_index"].GetInt();
    else continue;

    if (timerJson.HasMember("timer_type_id") && timerJson["timer_type_id"].IsInt())
      timer.timerTypeId = timerJson["timer_type_id"].GetInt();
    if (timerJson.HasMember("title") && timerJson["title"].IsString())
      timer.title = timerJson["title"].GetString();
    if (timerJson.HasMember("parent_client_index") && timerJson["parent_client_index"].IsInt())
      timer.parentClientIndex = timerJson["parent_client_index"].GetInt();

    if (timerJson.HasMember("client_channel_uid") && timerJson["client_channel_uid"].IsInt())
      timer.clientChannelUid = timerJson["client_channel_uid"].GetInt();
    if (timerJson.HasMember("channel_name") && timerJson["channel_name"].IsString())
      timer.channelName = timerJson["channel_name"].GetString();

    if (timerJson.HasMember("start_time") && timerJson["start_time"].IsString())
      timer.startTime = Utils::ParseISO8601(timerJson["start_time"].GetString());
    if (timerJson.HasMember("end_time") && timerJson["end_time"].IsString())
      timer.endTime = Utils::ParseISO8601(timerJson["end_time"].GetString());
    if (timerJson.HasMember("start_any_time") && timerJson["start_any_time"].IsBool())
      timer.startAnyTime = timerJson["start_any_time"].GetBool();
    if (timerJson.HasMember("end_any_time") && timerJson["end_any_time"].IsBool())
      timer.endAnyTime = timerJson["end_any_time"].GetBool();

    if (timerJson.HasMember("margin_start") && timerJson["margin_start"].IsInt())
      timer.marginStart = timerJson["margin_start"].GetInt();
    if (timerJson.HasMember("margin_end") && timerJson["margin_end"].IsInt())
      timer.marginEnd = timerJson["margin_end"].GetInt();

    if (timerJson.HasMember("state") && timerJson["state"].IsInt())
      timer.state = timerJson["state"].GetInt();

    if (timerJson.HasMember("weekdays") && timerJson["weekdays"].IsInt())
      timer.weekdays = timerJson["weekdays"].GetInt();
    if (timerJson.HasMember("first_day") && timerJson["first_day"].IsString())
      timer.firstDay = Utils::ParseISO8601(timerJson["first_day"].GetString());

    if (timerJson.HasMember("prevent_duplicate_episodes") && timerJson["prevent_duplicate_episodes"].IsInt())
      timer.preventDuplicateEpisodes = timerJson["prevent_duplicate_episodes"].GetInt();
    if (timerJson.HasMember("series_link") && timerJson["series_link"].IsString())
      timer.seriesLink = timerJson["series_link"].GetString();

    if (timerJson.HasMember("directory") && timerJson["directory"].IsString())
      timer.directory = timerJson["directory"].GetString();
    if (timerJson.HasMember("priority") && timerJson["priority"].IsInt())
      timer.priority = timerJson["priority"].GetInt();
    if (timerJson.HasMember("lifetime") && timerJson["lifetime"].IsInt())
      timer.lifetime = timerJson["lifetime"].GetInt();
    if (timerJson.HasMember("max_recordings") && timerJson["max_recordings"].IsInt())
      timer.maxRecordings = timerJson["max_recordings"].GetInt();
    if (timerJson.HasMember("recording_group") && timerJson["recording_group"].IsInt())
      timer.recordingGroup = timerJson["recording_group"].GetInt();

    if (timerJson.HasMember("epg_search_string") && timerJson["epg_search_string"].IsString())
      timer.epgSearchString = timerJson["epg_search_string"].GetString();
    if (timerJson.HasMember("full_text_epg_search") && timerJson["full_text_epg_search"].IsBool())
      timer.fullTextEpgSearch = timerJson["full_text_epg_search"].GetBool();
    if (timerJson.HasMember("epg_uid") && timerJson["epg_uid"].IsInt())
      timer.epgUid = timerJson["epg_uid"].GetInt();
    if (timerJson.HasMember("epg_event_id") && timerJson["epg_event_id"].IsString())
      timer.epgEventId = timerJson["epg_event_id"].GetString();

    if (timerJson.HasMember("genre_type") && timerJson["genre_type"].IsInt())
      timer.genreType = timerJson["genre_type"].GetInt();
    if (timerJson.HasMember("genre_sub_type") && timerJson["genre_sub_type"].IsInt())
      timer.genreSubType = timerJson["genre_sub_type"].GetInt();

    if (timerJson.HasMember("description") && timerJson["description"].IsString())
      timer.description = timerJson["description"].GetString();

    outTimers.push_back(timer);
  }
}

bool TimerManager::GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types) {
  std::shared_lock<std::shared_mutex> lock(m_dataMutex);
  for (const auto& timerType : m_timerTypes) {
    kodi::addon::PVRTimerType type;
    type.SetId(timerType.id);
    type.SetDescription(timerType.description);
    type.SetPriority(timerType.priority);
    type.SetMaxTimers(99);

    type.SetFeatures(
      PVR_TIMER_TYPE_SUPPORTS_START_TIME |
      PVR_TIMER_TYPE_SUPPORTS_END_TIME |
      PVR_TIMER_TYPE_SUPPORTS_PRIORITY |
      PVR_TIMER_TYPE_SUPPORTS_LIFETIME |
      PVR_TIMER_TYPE_SUPPORTS_RECORD_FOLDERS |
      PVR_TIMER_TYPE_SUPPORTS_ANYTIME |
      PVR_TIMER_TYPE_SUPPORTS_EPG_MATCH |
      PVR_TIMER_TYPE_SUPPORTS_MARGIN_START |
      PVR_TIMER_TYPE_SUPPORTS_MARGIN_END |
      PVR_TIMER_TYPE_SUPPORTS_WEEKDAYS |
      PVR_TIMER_TYPE_SUPPORTS_SERIES_LINK
    );
    types.push_back(type);
  }
  return true;
}

int TimerManager::GetTimersAmount() const {
  std::shared_lock<std::shared_mutex> lock(m_dataMutex);
  return static_cast<int>(m_timers.size());
}

bool TimerManager::GetTimers(kodi::addon::PVRTimersResultSet& results) {
  std::shared_lock<std::shared_mutex> lock(m_dataMutex);
  for (const auto& timer : m_timers) {
    kodi::addon::PVRTimer kodiTimer;
    MapTimerToKodi(timer, kodiTimer);
    results.Add(kodiTimer);
  }
  return true;
}

bool TimerManager::MapTimerToKodi(const UltimateTimer& timer, kodi::addon::PVRTimer& kodiTimer) {
  kodiTimer.SetClientIndex(timer.clientIndex);
  kodiTimer.SetTimerTypeId(timer.timerTypeId);
  kodiTimer.SetTitle(timer.title);
  kodiTimer.SetState(MapTimerStateToKodi(timer.state));
  kodiTimer.SetStartTime(timer.startTime);
  kodiTimer.SetEndTime(timer.endTime);

  if (timer.parentClientIndex > 0) kodiTimer.SetParentClientIndex(timer.parentClientIndex);
  kodiTimer.SetClientChannelUid(timer.clientChannelUid);
  if (!timer.channelName.empty()) kodiTimer.SetChannelName(timer.channelName);

  kodiTimer.SetStartAnyTime(timer.startAnyTime);
  kodiTimer.SetEndAnyTime(timer.endAnyTime);

  kodiTimer.SetMarginStart(timer.marginStart);
  kodiTimer.SetMarginEnd(timer.marginEnd);
  kodiTimer.SetPriority(timer.priority);
  kodiTimer.SetLifetime(timer.lifetime);
  kodiTimer.SetWeekdays(timer.weekdays);
  if (timer.firstDay > 0) kodiTimer.SetFirstDay(timer.firstDay);

  kodiTimer.SetSeriesLink(timer.seriesLink);

  kodiTimer.SetFullTextEpgSearch(timer.fullTextEpgSearch);
  if (!timer.epgSearchString.empty()) kodiTimer.SetEpgSearchString(timer.epgSearchString);
  if (timer.epgUid > 0) kodiTimer.SetEpgUid(timer.epgUid);
  if (!timer.directory.empty()) kodiTimer.SetDirectory(timer.directory);
  if (timer.maxRecordings > 0) kodiTimer.SetMaxRecordings(timer.maxRecordings);
  if (timer.recordingGroup > 0) kodiTimer.SetRecordingGroup(timer.recordingGroup);
  if (timer.genreType > 0) kodiTimer.SetGenreType(timer.genreType);
  if (timer.genreSubType > 0) kodiTimer.SetGenreSubType(timer.genreSubType);
  if (!timer.description.empty()) kodiTimer.SetDescription(timer.description);

  kodiTimer.SetPreventDuplicateEpisodes(
    static_cast<PVR_TIMER_DUPLICATEHANDLING>(timer.preventDuplicateEpisodes));

  return true;
}

bool TimerManager::MapKodiTimerToUltimate(const kodi::addon::PVRTimer& kodiTimer,
                                          UltimateTimer& ultimateTimer) {
  ultimateTimer.clientIndex = kodiTimer.GetClientIndex();
  ultimateTimer.timerTypeId = kodiTimer.GetTimerTypeId();
  ultimateTimer.title = kodiTimer.GetTitle();
  ultimateTimer.state = kodiTimer.GetState();
  ultimateTimer.startTime = kodiTimer.GetStartTime();
  ultimateTimer.endTime = kodiTimer.GetEndTime();
  ultimateTimer.parentClientIndex = kodiTimer.GetParentClientIndex();
  ultimateTimer.clientChannelUid = kodiTimer.GetClientChannelUid();
  ultimateTimer.channelName = kodiTimer.GetChannelName();
  ultimateTimer.startAnyTime = kodiTimer.GetStartAnyTime();
  ultimateTimer.endAnyTime = kodiTimer.GetEndAnyTime();
  ultimateTimer.marginStart = kodiTimer.GetMarginStart();
  ultimateTimer.marginEnd = kodiTimer.GetMarginEnd();
  ultimateTimer.priority = kodiTimer.GetPriority();
  ultimateTimer.lifetime = kodiTimer.GetLifetime();
  ultimateTimer.weekdays = kodiTimer.GetWeekdays();
  ultimateTimer.firstDay = kodiTimer.GetFirstDay();
  ultimateTimer.seriesLink = kodiTimer.GetSeriesLink();
  ultimateTimer.fullTextEpgSearch = kodiTimer.GetFullTextEpgSearch();
  ultimateTimer.epgSearchString = kodiTimer.GetEpgSearchString();
  ultimateTimer.epgUid = kodiTimer.GetEpgUid();
  ultimateTimer.directory = kodiTimer.GetDirectory();
  ultimateTimer.maxRecordings = kodiTimer.GetMaxRecordings();
  ultimateTimer.recordingGroup = kodiTimer.GetRecordingGroup();
  ultimateTimer.genreType = kodiTimer.GetGenreType();
  ultimateTimer.genreSubType = kodiTimer.GetGenreSubType();
  ultimateTimer.description = kodiTimer.GetDescription();
  ultimateTimer.preventDuplicateEpisodes = kodiTimer.GetPreventDuplicateEpisodes();

  return true;
}

PVR_TIMER_STATE TimerManager::MapTimerStateToKodi(int state) {
  switch (state) {
    case 0: return PVR_TIMER_STATE_SCHEDULED;
    case 1: return PVR_TIMER_STATE_SCHEDULED;
    case 2: return PVR_TIMER_STATE_RECORDING;
    case 3: return PVR_TIMER_STATE_COMPLETED;
    case 4: return PVR_TIMER_STATE_ABORTED;
    case 5: return PVR_TIMER_STATE_CANCELLED;
    case 6: return PVR_TIMER_STATE_CONFLICT_NOK;
    case 7: return PVR_TIMER_STATE_ERROR;
    default: return PVR_TIMER_STATE_SCHEDULED;
  }
}

bool TimerManager::AddTimer(const kodi::addon::PVRTimer& timer,
                            const std::vector<UltimateProvider>& providers,
                            const std::map<int, ChannelLookupInfo>& channelLookup,
                            const std::function<std::string(const std::string&)>& buildApiUrl,
                            const std::function<bool(const std::string&, const std::string&)>& httpPost,
                            const std::function<void()>& loadTimers) {
  UltimateTimer ultimateTimer;
  MapKodiTimerToUltimate(timer, ultimateTimer);

  std::string provider;

  if (timer.GetClientChannelUid() > 0) {
    auto it = channelLookup.find(timer.GetClientChannelUid());
    if (it != channelLookup.end()) provider = it->second.provider;
  }
  if (provider.empty() && !providers.empty()) {
    provider = providers[0].name;
  }

  if (provider.empty()) return false;

  rapidjson::Document doc(rapidjson::kObjectType);
  auto& allocator = doc.GetAllocator();
  doc.AddMember("timer_type_id", ultimateTimer.timerTypeId, allocator);
  doc.AddMember("title", rapidjson::Value(ultimateTimer.title.c_str(), allocator), allocator);
  doc.AddMember("provider", rapidjson::Value(provider.c_str(), allocator), allocator);
  doc.AddMember("client_channel_uid", ultimateTimer.clientChannelUid, allocator);

  if (ultimateTimer.startTime > 0)
    doc.AddMember("start_time", rapidjson::Value(Utils::ToISO8601(ultimateTimer.startTime).c_str(), allocator), allocator);
  if (ultimateTimer.endTime > 0)
    doc.AddMember("end_time", rapidjson::Value(Utils::ToISO8601(ultimateTimer.endTime).c_str(), allocator), allocator);
  if (ultimateTimer.priority > 0) doc.AddMember("priority", ultimateTimer.priority, allocator);
  if (ultimateTimer.lifetime > 0) doc.AddMember("lifetime", ultimateTimer.lifetime, allocator);
  if (ultimateTimer.marginStart > 0) doc.AddMember("margin_start", ultimateTimer.marginStart, allocator);
  if (ultimateTimer.marginEnd > 0) doc.AddMember("margin_end", ultimateTimer.marginEnd, allocator);
  if (ultimateTimer.weekdays > 0) doc.AddMember("weekdays", ultimateTimer.weekdays, allocator);
  if (ultimateTimer.firstDay > 0)
    doc.AddMember("first_day", rapidjson::Value(Utils::ToISO8601(ultimateTimer.firstDay).c_str(), allocator), allocator);
  if (!ultimateTimer.seriesLink.empty())
    doc.AddMember("series_link", rapidjson::Value(ultimateTimer.seriesLink.c_str(), allocator), allocator);
  if (ultimateTimer.preventDuplicateEpisodes > 0)
    doc.AddMember("prevent_duplicate_episodes", ultimateTimer.preventDuplicateEpisodes, allocator);
  if (!ultimateTimer.epgSearchString.empty())
    doc.AddMember("epg_search_string", rapidjson::Value(ultimateTimer.epgSearchString.c_str(), allocator), allocator);
  doc.AddMember("full_text_epg_search", ultimateTimer.fullTextEpgSearch, allocator);
  if (ultimateTimer.epgUid > 0) doc.AddMember("epg_uid", ultimateTimer.epgUid, allocator);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  if (!httpPost(buildApiUrl("/api/providers/" + provider + "/timers"), buffer.GetString())) {
    return false;
  }

  loadTimers();
  return true;
}

bool TimerManager::DeleteTimer(int clientIndex, bool forceDelete,
                               const std::function<std::string(const std::string&)>& buildApiUrl,
                               const std::function<bool(const std::string&)>& httpDelete,
                               const std::function<void()>& loadTimers) {
  std::string provider;
  bool found = false;

  {
    std::shared_lock<std::shared_mutex> lock(m_dataMutex);
    UltimateTimer* ultimateTimer = FindTimer(clientIndex);
    if (!ultimateTimer) return false;
    provider = ultimateTimer->provider;
    found = true;
  }

  if (!found) return false;

  std::string url = buildApiUrl("/api/providers/" + provider + "/timers/" + std::to_string(clientIndex));
  if (forceDelete) url += "?force=true";

  if (!httpDelete(url)) return false;

  loadTimers();
  return true;
}

bool TimerManager::UpdateTimer(const kodi::addon::PVRTimer& timer,
                               const std::function<std::string(const std::string&)>& buildApiUrl,
                               const std::function<bool(const std::string&, const std::string&)>& httpPut,
                               const std::function<void()>& loadTimers) {
  int clientIndex = timer.GetClientIndex();

  std::shared_lock<std::shared_mutex> lock(m_dataMutex);
  UltimateTimer* existingTimer = FindTimer(clientIndex);
  if (!existingTimer) return false;

  UltimateTimer updatedTimer;
  MapKodiTimerToUltimate(timer, updatedTimer);
  updatedTimer.provider = existingTimer->provider;
  lock.unlock();

  rapidjson::Document doc(rapidjson::kObjectType);
  auto& allocator = doc.GetAllocator();
  doc.AddMember("timer_type_id", updatedTimer.timerTypeId, allocator);
  doc.AddMember("title", rapidjson::Value(updatedTimer.title.c_str(), allocator), allocator);
  doc.AddMember("client_channel_uid", updatedTimer.clientChannelUid, allocator);

  if (updatedTimer.startTime > 0)
    doc.AddMember("start_time", rapidjson::Value(Utils::ToISO8601(updatedTimer.startTime).c_str(), allocator), allocator);
  if (updatedTimer.endTime > 0)
    doc.AddMember("end_time", rapidjson::Value(Utils::ToISO8601(updatedTimer.endTime).c_str(), allocator), allocator);
  if (updatedTimer.priority > 0) doc.AddMember("priority", updatedTimer.priority, allocator);
  if (updatedTimer.lifetime > 0) doc.AddMember("lifetime", updatedTimer.lifetime, allocator);
  if (updatedTimer.marginStart > 0) doc.AddMember("margin_start", updatedTimer.marginStart, allocator);
  if (updatedTimer.marginEnd > 0) doc.AddMember("margin_end", updatedTimer.marginEnd, allocator);
  if (updatedTimer.weekdays > 0) doc.AddMember("weekdays", updatedTimer.weekdays, allocator);
  if (updatedTimer.firstDay > 0)
    doc.AddMember("first_day", rapidjson::Value(Utils::ToISO8601(updatedTimer.firstDay).c_str(), allocator), allocator);
  if (!updatedTimer.seriesLink.empty())
    doc.AddMember("series_link", rapidjson::Value(updatedTimer.seriesLink.c_str(), allocator), allocator);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  if (!httpPut(buildApiUrl("/api/providers/" + updatedTimer.provider + "/timers/" + std::to_string(clientIndex)),
               buffer.GetString())) {
    return false;
  }

  loadTimers();
  return true;
}

UltimateTimer* TimerManager::FindTimer(int clientIndex) {
  for (auto& timer : m_timers) {
    if (timer.clientIndex == clientIndex) return &timer;
  }
  return nullptr;
}