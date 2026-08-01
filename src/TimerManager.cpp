#include "TimerManager.h"
#include "Utils.h"

bool TimerManager::LoadTimerTypes(const std::vector<UltimateProvider>& providers,
                                  const std::function<std::string(const std::string&)>& httpGet,
                                  const std::function<bool(const std::string&, nlohmann::json&)>& parseJson) {
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
                                             const std::function<bool(const std::string&, nlohmann::json&)>& parseJson,
                                             std::vector<UltimateTimerType>& outTimerTypes) {
  std::string response = httpGet("/api/providers/" + Utils::UrlPathEncode(provider) + "/timer-types");
  if (response.empty()) return;

  nlohmann::json document;
  if (!parseJson(response, document)) return;
  if (!document.contains("timer_types") || !document["timer_types"].is_array()) return;

  for (const auto& ttJson : document["timer_types"]) {
    UltimateTimerType timerType;
    if (ttJson.contains("id") && ttJson["id"].is_number_integer()) timerType.id = ttJson["id"].get<int>();
    else continue;
    timerType.description = (ttJson.contains("description") && ttJson["description"].is_string()) ? ttJson["description"].get<std::string>() : "Timer";
    timerType.priority = (ttJson.contains("priority") && ttJson["priority"].is_number_integer()) ? ttJson["priority"].get<int>() : 50;
    outTimerTypes.push_back(timerType);
  }
}

bool TimerManager::LoadTimers(const std::vector<UltimateProvider>& providers,
                              const std::function<std::string(const std::string&)>& httpGet,
                              const std::function<bool(const std::string&, nlohmann::json&)>& parseJson) {
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
                                         const std::function<bool(const std::string&, nlohmann::json&)>& parseJson,
                                         std::vector<UltimateTimer>& outTimers) {
  std::string response = httpGet("/api/providers/" + Utils::UrlPathEncode(provider) + "/timers?include_inactive=true");
  if (response.empty()) return;

  nlohmann::json document;
  if (!parseJson(response, document)) return;
  if (!document.contains("timers") || !document["timers"].is_array()) return;

  for (const auto& timerJson : document["timers"]) {
    UltimateTimer timer;
    timer.provider = provider;

    if (timerJson.contains("client_index") && timerJson["client_index"].is_number_integer())
      timer.clientIndex = timerJson["client_index"].get<int>();
    else continue;

    if (timerJson.contains("timer_type_id") && timerJson["timer_type_id"].is_number_integer())
      timer.timerTypeId = timerJson["timer_type_id"].get<int>();
    if (timerJson.contains("title") && timerJson["title"].is_string())
      timer.title = timerJson["title"].get<std::string>();
    if (timerJson.contains("parent_client_index") && timerJson["parent_client_index"].is_number_integer())
      timer.parentClientIndex = timerJson["parent_client_index"].get<int>();

    if (timerJson.contains("client_channel_uid") && timerJson["client_channel_uid"].is_number_integer())
      timer.clientChannelUid = timerJson["client_channel_uid"].get<int>();
    if (timerJson.contains("channel_name") && timerJson["channel_name"].is_string())
      timer.channelName = timerJson["channel_name"].get<std::string>();

    if (timerJson.contains("start_time") && timerJson["start_time"].is_string())
      timer.startTime = Utils::ParseISO8601(timerJson["start_time"].get<std::string>());
    if (timerJson.contains("end_time") && timerJson["end_time"].is_string())
      timer.endTime = Utils::ParseISO8601(timerJson["end_time"].get<std::string>());
    if (timerJson.contains("start_any_time") && timerJson["start_any_time"].is_boolean())
      timer.startAnyTime = timerJson["start_any_time"].get<bool>();
    if (timerJson.contains("end_any_time") && timerJson["end_any_time"].is_boolean())
      timer.endAnyTime = timerJson["end_any_time"].get<bool>();

    if (timerJson.contains("margin_start") && timerJson["margin_start"].is_number_integer())
      timer.marginStart = timerJson["margin_start"].get<int>();
    if (timerJson.contains("margin_end") && timerJson["margin_end"].is_number_integer())
      timer.marginEnd = timerJson["margin_end"].get<int>();

    if (timerJson.contains("state") && timerJson["state"].is_number_integer())
      timer.state = timerJson["state"].get<int>();

    if (timerJson.contains("weekdays") && timerJson["weekdays"].is_number_integer())
      timer.weekdays = timerJson["weekdays"].get<int>();
    if (timerJson.contains("first_day") && timerJson["first_day"].is_string())
      timer.firstDay = Utils::ParseISO8601(timerJson["first_day"].get<std::string>());

    if (timerJson.contains("prevent_duplicate_episodes") && timerJson["prevent_duplicate_episodes"].is_number_integer())
      timer.preventDuplicateEpisodes = timerJson["prevent_duplicate_episodes"].get<int>();
    if (timerJson.contains("series_link") && timerJson["series_link"].is_string())
      timer.seriesLink = timerJson["series_link"].get<std::string>();

    if (timerJson.contains("directory") && timerJson["directory"].is_string())
      timer.directory = timerJson["directory"].get<std::string>();
    if (timerJson.contains("priority") && timerJson["priority"].is_number_integer())
      timer.priority = timerJson["priority"].get<int>();
    if (timerJson.contains("lifetime") && timerJson["lifetime"].is_number_integer())
      timer.lifetime = timerJson["lifetime"].get<int>();
    if (timerJson.contains("max_recordings") && timerJson["max_recordings"].is_number_integer())
      timer.maxRecordings = timerJson["max_recordings"].get<int>();
    if (timerJson.contains("recording_group") && timerJson["recording_group"].is_number_integer())
      timer.recordingGroup = timerJson["recording_group"].get<int>();

    if (timerJson.contains("epg_search_string") && timerJson["epg_search_string"].is_string())
      timer.epgSearchString = timerJson["epg_search_string"].get<std::string>();
    if (timerJson.contains("full_text_epg_search") && timerJson["full_text_epg_search"].is_boolean())
      timer.fullTextEpgSearch = timerJson["full_text_epg_search"].get<bool>();
    if (timerJson.contains("epg_uid") && timerJson["epg_uid"].is_number_integer())
      timer.epgUid = timerJson["epg_uid"].get<int>();
    if (timerJson.contains("epg_event_id") && timerJson["epg_event_id"].is_string())
      timer.epgEventId = timerJson["epg_event_id"].get<std::string>();

    if (timerJson.contains("genre_type") && timerJson["genre_type"].is_number_integer())
      timer.genreType = timerJson["genre_type"].get<int>();
    if (timerJson.contains("genre_sub_type") && timerJson["genre_sub_type"].is_number_integer())
      timer.genreSubType = timerJson["genre_sub_type"].get<int>();

    if (timerJson.contains("description") && timerJson["description"].is_string())
      timer.description = timerJson["description"].get<std::string>();

    outTimers.push_back(timer);
  }
}

bool TimerManager::GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types) const {
  std::shared_lock<std::shared_mutex> lock(m_dataMutex);
  for (const auto& timerType : m_timerTypes) {
    kodi::addon::PVRTimerType type;
    type.SetId(timerType.id);
    type.SetDescription(timerType.description);
    type.SetPrioritiesDefault(timerType.priority);

    type.SetAttributes(
      PVR_TIMER_TYPE_SUPPORTS_START_TIME |
      PVR_TIMER_TYPE_SUPPORTS_END_TIME |
      PVR_TIMER_TYPE_SUPPORTS_PRIORITY |
      PVR_TIMER_TYPE_SUPPORTS_LIFETIME |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_FOLDERS |
      PVR_TIMER_TYPE_SUPPORTS_START_ANYTIME |
      PVR_TIMER_TYPE_SUPPORTS_END_ANYTIME |
      PVR_TIMER_TYPE_SUPPORTS_FULLTEXT_EPG_MATCH |
      PVR_TIMER_TYPE_SUPPORTS_START_MARGIN |
      PVR_TIMER_TYPE_SUPPORTS_END_MARGIN |
      PVR_TIMER_TYPE_SUPPORTS_WEEKDAYS
    );
    types.push_back(type);
  }
  return true;
}

int TimerManager::GetTimersAmount() const {
  std::shared_lock<std::shared_mutex> lock(m_dataMutex);
  return static_cast<int>(m_timers.size());
}

bool TimerManager::GetTimers(kodi::addon::PVRTimersResultSet& results) const {
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
  kodiTimer.SetTimerType(timer.timerTypeId);
  kodiTimer.SetTitle(timer.title);
  kodiTimer.SetState(MapTimerStateToKodi(timer.state));
  kodiTimer.SetStartTime(timer.startTime);
  kodiTimer.SetEndTime(timer.endTime);

  if (timer.parentClientIndex > 0) kodiTimer.SetParentClientIndex(timer.parentClientIndex);
  kodiTimer.SetClientChannelUid(timer.clientChannelUid);
  // Note: kodi::addon::PVRTimer has no channel-name field -- Kodi resolves
  // the displayed channel name itself from ClientChannelUid, so timer.channelName
  // (kept on UltimateTimer for our own bookkeeping) is intentionally not sent.

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
  if (!timer.epgSearchString.empty()) kodiTimer.SetEPGSearchString(timer.epgSearchString);
  if (timer.epgUid > 0) kodiTimer.SetEPGUid(timer.epgUid);
  if (!timer.directory.empty()) kodiTimer.SetDirectory(timer.directory);
  if (timer.maxRecordings > 0) kodiTimer.SetMaxRecordings(timer.maxRecordings);
  if (timer.recordingGroup > 0) kodiTimer.SetRecordingGroup(timer.recordingGroup);
  if (timer.genreType > 0) kodiTimer.SetGenreType(timer.genreType);
  if (timer.genreSubType > 0) kodiTimer.SetGenreSubType(timer.genreSubType);
  if (!timer.description.empty()) kodiTimer.SetSummary(timer.description);

  kodiTimer.SetPreventDuplicateEpisodes(
    static_cast<unsigned int>(timer.preventDuplicateEpisodes));

  return true;
}

bool TimerManager::MapKodiTimerToUltimate(const kodi::addon::PVRTimer& kodiTimer,
                                          UltimateTimer& ultimateTimer) {
  ultimateTimer.clientIndex = kodiTimer.GetClientIndex();
  ultimateTimer.timerTypeId = kodiTimer.GetTimerType();
  ultimateTimer.title = kodiTimer.GetTitle();
  ultimateTimer.state = kodiTimer.GetState();
  ultimateTimer.startTime = kodiTimer.GetStartTime();
  ultimateTimer.endTime = kodiTimer.GetEndTime();
  ultimateTimer.parentClientIndex = kodiTimer.GetParentClientIndex();
  ultimateTimer.clientChannelUid = kodiTimer.GetClientChannelUid();
  // Note: no GetChannelName() on kodi::addon::PVRTimer -- if we need the
  // channel name here, it has to be resolved from clientChannelUid via the
  // channel lookup, not read back off the Kodi timer object.
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
  ultimateTimer.epgSearchString = kodiTimer.GetEPGSearchString();
  ultimateTimer.epgUid = kodiTimer.GetEPGUid();
  ultimateTimer.directory = kodiTimer.GetDirectory();
  ultimateTimer.maxRecordings = kodiTimer.GetMaxRecordings();
  ultimateTimer.recordingGroup = kodiTimer.GetRecordingGroup();
  ultimateTimer.genreType = kodiTimer.GetGenreType();
  ultimateTimer.genreSubType = kodiTimer.GetGenreSubType();
  ultimateTimer.description = kodiTimer.GetSummary();
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

  nlohmann::json doc = nlohmann::json::object();
  doc["timer_type_id"] = ultimateTimer.timerTypeId;
  doc["title"] = ultimateTimer.title;
  doc["provider"] = provider;
  doc["client_channel_uid"] = ultimateTimer.clientChannelUid;

  if (ultimateTimer.startTime > 0)
    doc["start_time"] = Utils::ToISO8601(ultimateTimer.startTime);
  if (ultimateTimer.endTime > 0)
    doc["end_time"] = Utils::ToISO8601(ultimateTimer.endTime);
  if (ultimateTimer.priority > 0) doc["priority"] = ultimateTimer.priority;
  if (ultimateTimer.lifetime > 0) doc["lifetime"] = ultimateTimer.lifetime;
  if (ultimateTimer.marginStart > 0) doc["margin_start"] = ultimateTimer.marginStart;
  if (ultimateTimer.marginEnd > 0) doc["margin_end"] = ultimateTimer.marginEnd;
  if (ultimateTimer.weekdays > 0) doc["weekdays"] = ultimateTimer.weekdays;
  if (ultimateTimer.firstDay > 0)
    doc["first_day"] = Utils::ToISO8601(ultimateTimer.firstDay);
  if (!ultimateTimer.seriesLink.empty())
    doc["series_link"] = ultimateTimer.seriesLink;
  if (ultimateTimer.preventDuplicateEpisodes > 0)
    doc["prevent_duplicate_episodes"] = ultimateTimer.preventDuplicateEpisodes;
  if (!ultimateTimer.epgSearchString.empty())
    doc["epg_search_string"] = ultimateTimer.epgSearchString;
  doc["full_text_epg_search"] = ultimateTimer.fullTextEpgSearch;
  if (ultimateTimer.epgUid > 0) doc["epg_uid"] = ultimateTimer.epgUid;

  if (!httpPost(buildApiUrl("/api/providers/" + Utils::UrlPathEncode(provider) + "/timers"), doc.dump())) {
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

  std::string url = buildApiUrl("/api/providers/" + Utils::UrlPathEncode(provider) + "/timers/" + std::to_string(clientIndex));
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

  nlohmann::json doc = nlohmann::json::object();
  doc["timer_type_id"] = updatedTimer.timerTypeId;
  doc["title"] = updatedTimer.title;
  doc["client_channel_uid"] = updatedTimer.clientChannelUid;

  if (updatedTimer.startTime > 0)
    doc["start_time"] = Utils::ToISO8601(updatedTimer.startTime);
  if (updatedTimer.endTime > 0)
    doc["end_time"] = Utils::ToISO8601(updatedTimer.endTime);
  if (updatedTimer.priority > 0) doc["priority"] = updatedTimer.priority;
  if (updatedTimer.lifetime > 0) doc["lifetime"] = updatedTimer.lifetime;
  if (updatedTimer.marginStart > 0) doc["margin_start"] = updatedTimer.marginStart;
  if (updatedTimer.marginEnd > 0) doc["margin_end"] = updatedTimer.marginEnd;
  if (updatedTimer.weekdays > 0) doc["weekdays"] = updatedTimer.weekdays;
  if (updatedTimer.firstDay > 0)
    doc["first_day"] = Utils::ToISO8601(updatedTimer.firstDay);
  if (!updatedTimer.seriesLink.empty())
    doc["series_link"] = updatedTimer.seriesLink;
  if (updatedTimer.preventDuplicateEpisodes > 0)
    doc["prevent_duplicate_episodes"] = updatedTimer.preventDuplicateEpisodes;
  if (!updatedTimer.epgSearchString.empty())
    doc["epg_search_string"] = updatedTimer.epgSearchString;
  doc["full_text_epg_search"] = updatedTimer.fullTextEpgSearch;
  if (updatedTimer.epgUid > 0) doc["epg_uid"] = updatedTimer.epgUid;

  if (!httpPut(buildApiUrl("/api/providers/" + Utils::UrlPathEncode(updatedTimer.provider) + "/timers/" + std::to_string(clientIndex)),
               doc.dump())) {
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