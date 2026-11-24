#include "TimerManager.h"
#include "Utils.h"
#include "EPGManager.h"
#include <cctype>

namespace {

// The backend serialises PascalCase (Timer.to_dict / TimerType.to_dict, e.g. "ClientIndex",
// "EpgEventId"); snake_case is accepted as well, like ultimate-ui's pick(). Null counts as absent.
const nlohmann::json* Pick(const nlohmann::json& obj, const char* pascal, const char* snake) {
  for (const char* key : {pascal, snake}) {
    auto it = obj.find(key);
    if (it != obj.end() && !it->is_null()) return &*it;
  }
  return nullptr;
}

bool PickInt(const nlohmann::json& obj, const char* pascal, const char* snake, int& out) {
  const nlohmann::json* v = Pick(obj, pascal, snake);
  if (!v || !v->is_number_integer()) return false;
  out = v->get<int>();
  return true;
}

bool PickBool(const nlohmann::json& obj, const char* pascal, const char* snake, bool& out) {
  const nlohmann::json* v = Pick(obj, pascal, snake);
  if (!v || !v->is_boolean()) return false;
  out = v->get<bool>();
  return true;
}

bool PickString(const nlohmann::json& obj, const char* pascal, const char* snake, std::string& out) {
  const nlohmann::json* v = Pick(obj, pascal, snake);
  if (!v || !v->is_string()) return false;
  out = v->get<std::string>();
  return true;
}

bool PickTime(const nlohmann::json& obj, const char* pascal, const char* snake, time_t& out) {
  std::string iso;
  if (!PickString(obj, pascal, snake, iso)) return false;
  out = Utils::ParseISO8601(iso);
  return true;
}

// Backend TimerState (a string enum) -> the numbering MapTimerStateToKodi() expects
// (declaration order of the enum). Plain integers pass through unchanged.
int ParseTimerState(const nlohmann::json& obj) {
  const nlohmann::json* v = Pick(obj, "State", "state");
  if (!v) return 0;
  if (v->is_number_integer()) return v->get<int>();
  if (!v->is_string()) return 0;
  std::string name = v->get<std::string>();
  for (char& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  static const char* const kStates[] = {"NEW", "SCHEDULED", "RECORDING", "COMPLETED",
                                        "ABORTED", "CANCELLED", "CONFLICT", "ERROR"};
  for (int i = 0; i < 8; ++i) {
    if (name == kStates[i]) return i;
  }
  return 0;
}

// Python's datetime.fromisoformat (< 3.11) cannot read a trailing "Z": send "+00:00" like
// ultimate-ui's toBackendIso.
std::string ToBackendIso(time_t t) {
  std::string iso = Utils::ToISO8601(t);
  if (!iso.empty() && iso.back() == 'Z') iso.replace(iso.size() - 1, 1, "+00:00");
  return iso;
}

}  // namespace

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
    if (!PickInt(ttJson, "TypeId", "type_id", timerType.id) &&
        !PickInt(ttJson, "Id", "id", timerType.id))
      continue;
    timerType.description = "Timer";
    PickString(ttJson, "Description", "description", timerType.description);
    PickInt(ttJson, "PriorityDefault", "priority_default", timerType.priority);
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

    if (!PickInt(timerJson, "ClientIndex", "client_index", timer.clientIndex)) continue;

    PickInt(timerJson, "TimerTypeId", "timer_type_id", timer.timerTypeId);
    PickString(timerJson, "Title", "title", timer.title);
    PickInt(timerJson, "ParentClientIndex", "parent_client_index", timer.parentClientIndex);

    PickInt(timerJson, "ClientChannelUid", "client_channel_uid", timer.clientChannelUid);
    PickString(timerJson, "ChannelName", "channel_name", timer.channelName);

    PickTime(timerJson, "StartTime", "start_time", timer.startTime);
    PickTime(timerJson, "EndTime", "end_time", timer.endTime);
    PickBool(timerJson, "StartAnyTime", "start_any_time", timer.startAnyTime);
    PickBool(timerJson, "EndAnyTime", "end_any_time", timer.endAnyTime);
    PickTime(timerJson, "FirstDay", "first_day", timer.firstDay);

    PickInt(timerJson, "MarginStart", "margin_start", timer.marginStart);
    PickInt(timerJson, "MarginEnd", "margin_end", timer.marginEnd);

    timer.state = ParseTimerState(timerJson);

    PickInt(timerJson, "Weekdays", "weekdays", timer.weekdays);
    PickInt(timerJson, "PreventDuplicateEpisodes", "prevent_duplicate_episodes",
            timer.preventDuplicateEpisodes);
    PickString(timerJson, "SeriesLink", "series_link", timer.seriesLink);

    PickString(timerJson, "Directory", "directory", timer.directory);
    PickInt(timerJson, "Priority", "priority", timer.priority);
    PickInt(timerJson, "Lifetime", "lifetime", timer.lifetime);
    PickInt(timerJson, "MaxRecordings", "max_recordings", timer.maxRecordings);
    PickInt(timerJson, "RecordingGroup", "recording_group", timer.recordingGroup);

    PickString(timerJson, "EpgSearchString", "epg_search_string", timer.epgSearchString);
    PickBool(timerJson, "FullTextEpgSearch", "full_text_epg_search", timer.fullTextEpgSearch);
    PickInt(timerJson, "EpgUid", "epg_uid", timer.epgUid);
    PickString(timerJson, "EpgEventId", "epg_event_id", timer.epgEventId);

    PickInt(timerJson, "GenreType", "genre_type", timer.genreType);
    PickInt(timerJson, "GenreSubType", "genre_sub_type", timer.genreSubType);

    PickString(timerJson, "Description", "description", timer.description);

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
  // Kodi links a timer to its EPG tag through iEpgUid == the tag's UniqueBroadcastId,
  // which is computed locally. The provider's epg_event_id is the stable key, so map
  // it back to the broadcast id first; the backend's own epg_uid is only a fallback
  // (it matches for timers created by this addon, not for ones made in ultimate-ui).
  unsigned int epgUid = EPGManager::GetBroadcastIdForEventId(timer.epgEventId);
  if (epgUid == 0 && timer.epgUid > 0) epgUid = static_cast<unsigned int>(timer.epgUid);
  if (epgUid > 0) kodiTimer.SetEPGUid(epgUid);
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

std::string TimerManager::ResolveEpgEventId(const kodi::addon::PVRTimer& kodiTimer,
                                            const std::string& storedEventId) {
  const unsigned int epgUid = kodiTimer.GetEPGUid();
  if (epgUid > 0) {
    std::string eventId = EPGManager::GetEpgEventId(epgUid);
    if (!eventId.empty()) return eventId;
  }
  return storedEventId;
}

nlohmann::json TimerManager::BuildTimerBody(const UltimateTimer& timer,
                                            const std::string& epgEventId) {
  nlohmann::json doc = nlohmann::json::object();
  doc["timer_type_id"] = timer.timerTypeId;
  doc["title"] = timer.title;
  doc["client_channel_uid"] = timer.clientChannelUid;

  if (timer.startTime > 0) doc["start_time"] = ToBackendIso(timer.startTime);
  if (timer.endTime > 0) doc["end_time"] = ToBackendIso(timer.endTime);
  if (timer.priority > 0) doc["priority"] = timer.priority;
  if (timer.lifetime > 0) doc["lifetime"] = timer.lifetime;
  if (timer.marginStart > 0) doc["margin_start"] = timer.marginStart;
  if (timer.marginEnd > 0) doc["margin_end"] = timer.marginEnd;
  if (timer.weekdays > 0) doc["weekdays"] = timer.weekdays;
  if (timer.firstDay > 0) doc["first_day"] = ToBackendIso(timer.firstDay);
  if (!timer.seriesLink.empty()) doc["series_link"] = timer.seriesLink;
  if (timer.preventDuplicateEpisodes > 0)
    doc["prevent_duplicate_episodes"] = timer.preventDuplicateEpisodes;
  if (!timer.epgSearchString.empty()) doc["epg_search_string"] = timer.epgSearchString;
  doc["full_text_epg_search"] = timer.fullTextEpgSearch;

  // The provider's event id is what the backend keys EPG-based timers on; epg_uid
  // (our local broadcast id) is only an extra hint, as in ultimate-ui.
  if (timer.epgUid > 0) doc["epg_uid"] = timer.epgUid;
  if (!epgEventId.empty()) doc["epg_event_id"] = epgEventId;
  return doc;
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

  const std::string epgEventId = ResolveEpgEventId(timer, "");
  if (epgEventId.empty() && ultimateTimer.epgUid > 0) {
    // The EPG tag was not (or no longer) seen with an epg_event_id: the timer is sent
    // without one and the backend decides whether that is acceptable.
    kodi::Log(ADDON_LOG_WARNING,
              "AddTimer '%s' (%s): EPG tag %d has no epg_event_id, creating timer without it",
              ultimateTimer.title.c_str(), provider.c_str(), ultimateTimer.epgUid);
  }

  nlohmann::json doc = BuildTimerBody(ultimateTimer, epgEventId);
  doc["provider"] = provider;
  doc["state"] = "SCHEDULED";  // backend default would be NEW; ultimate-ui sends SCHEDULED

  if (!httpPost(buildApiUrl("/api/providers/" + Utils::UrlPathEncode(provider) + "/timers"), doc.dump())) {
    return false;
  }

  loadTimers();
  return true;
}

bool TimerManager::DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete,
                               const std::function<std::string(const std::string&)>& buildApiUrl,
                               const std::function<bool(const std::string&)>& httpDelete,
                               const std::function<void()>& loadTimers) {
  const int clientIndex = timer.GetClientIndex();
  std::string provider;
  std::string storedEventId;

  {
    std::shared_lock<std::shared_mutex> lock(m_dataMutex);
    const UltimateTimer* existing = FindTimer(clientIndex);
    if (!existing) return false;
    provider = existing->provider;
    storedEventId = existing->epgEventId;
  }

  // The backend's own record of the timer is authoritative for delete; the EPG cache
  // only fills in when the backend never reported an event id for it.
  std::string epgEventId = !storedEventId.empty() ? storedEventId : ResolveEpgEventId(timer, "");

  // DELETE has no body: force and the provider event id travel as query parameters
  // (same contract as ultimate-ui's DELETE /timers/<client_index>).
  std::string url = buildApiUrl("/api/providers/" + Utils::UrlPathEncode(provider) + "/timers/" +
                                std::to_string(clientIndex));
  char sep = '?';
  if (forceDelete) {
    url += sep;
    url += "force=true";
    sep = '&';
  }
  if (!epgEventId.empty()) {
    url += sep;
    url += "epg_event_id=" + Utils::UrlEncode(epgEventId);
  }

  if (!httpDelete(url)) return false;

  loadTimers();
  return true;
}

bool TimerManager::UpdateTimer(const kodi::addon::PVRTimer& timer,
                               const std::function<std::string(const std::string&)>& buildApiUrl,
                               const std::function<bool(const std::string&, const std::string&)>& httpPut,
                               const std::function<void()>& loadTimers) {
  const int clientIndex = timer.GetClientIndex();

  UltimateTimer updatedTimer;
  std::string storedEventId;
  {
    std::shared_lock<std::shared_mutex> lock(m_dataMutex);
    const UltimateTimer* existing = FindTimer(clientIndex);
    if (!existing) return false;

    MapKodiTimerToUltimate(timer, updatedTimer);
    updatedTimer.provider = existing->provider;
    storedEventId = existing->epgEventId;
  }

  // PUT keeps the timer's client_index (path); the body carries the epg_event_id, which
  // stays the one the timer was created for unless Kodi re-points it at another tag.
  const std::string epgEventId = ResolveEpgEventId(timer, storedEventId);
  nlohmann::json doc = BuildTimerBody(updatedTimer, epgEventId);

  if (!httpPut(buildApiUrl("/api/providers/" + Utils::UrlPathEncode(updatedTimer.provider) + "/timers/" +
                           std::to_string(clientIndex)),
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

const UltimateTimer* TimerManager::FindTimer(int clientIndex) const {
  for (const auto& timer : m_timers) {
    if (timer.clientIndex == clientIndex) return &timer;
  }
  return nullptr;
}