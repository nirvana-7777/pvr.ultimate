#pragma once

#include "Models.h"
#include <kodi/addon-instance/PVR.h>
#include <vector>
#include <map>
#include <shared_mutex>
#include <functional>
#include <string>
#include <nlohmann/json.hpp>

class EPGManager {
public:
    EPGManager() = default;

    static bool GetEPGForChannel(int channelUid, time_t start, time_t end,
                                 const std::function<std::string(const std::string&)>& httpGet,
                                 const std::function<bool(const std::string&, nlohmann::json&)>& parseJson,
                                 const std::function<bool(int, UltimateChannel&)>& getChannelByUid,
                                 kodi::addon::PVREPGTagsResultSet& results);

    // Extended overload: optionally try a database EPG service first via httpGetAbsolute,
    // falling back to the backend API (httpGet) on empty response or parse failure.
    static bool GetEPGForChannel(int channelUid, time_t start, time_t end,
                                 const std::function<std::string(const std::string&)>& httpGet,
                                 const std::function<bool(const std::string&, nlohmann::json&)>& parseJson,
                                 const std::function<bool(int, UltimateChannel&)>& getChannelByUid,
                                 kodi::addon::PVREPGTagsResultSet& results,
                                 const std::function<std::string(const std::string&)>& httpGetAbsolute,
                                 bool useDatabaseEpg);

    static bool IsEPGTagRecordable(const kodi::addon::PVREPGTag& tag, bool& isRecordable);
    static bool IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& isPlayable,
                          const std::function<bool(int, std::string&, std::string&, int&)>& getChannelInfo);

    // Fetches the channel manifest (same endpoint/contract as live playback) and builds the
    // catchup stream URL from the backend-provided catchup_stream_url_template, substituting
    // {start_time}/{end_time} (always required) and {epg_id}/{country} (only if the template
    // actually contains those placeholders - the template is the single source of truth for
    // what the backend needs, the client does not decide which params to send).
    //
    // On success, drmConfigsBase64/streamHeadersBase64 are populated from the manifest response
    // (only when supportsPiggyback is true) so the caller can apply DRM and stream headers via
    // the same ApplyDRMProperties/ApplyStreamHeaders path used for live channels.
    static bool GetEPGTagStreamProperties(const kodi::addon::PVREPGTag& tag,
                                          std::vector<kodi::addon::PVRStreamProperty>& properties,
                                          const std::function<std::string(const std::string&)>& httpGet,
                                          const std::function<bool(const std::string&, nlohmann::json&)>& parseJson,
                                          const std::function<bool(int, std::string&, std::string&, int&)>& getChannelInfo,
                                          const std::function<bool(int, UltimateChannel&)>& getChannelByUid,
                                          const std::function<bool()>& isBackendAvailable,
                                          const std::function<bool(const std::string&)>& retryBackendCall,
                                          const std::function<std::string(const std::string&, const std::string&)>& getManifestUrl,
                                          const std::function<bool(const std::string&, std::string&, std::string&, std::string&)>& httpGetWithHeaders,
                                          bool supportsPiggyback,
                                          std::string& drmConfigsBase64,
                                          std::string& streamHeadersBase64);

    // Look up the provider-native listing guid (e.g. "das_erste_hd_031eace5")
    // for a broadcast previously seen via GetEPGForChannel, keyed by the same
    // locally-computed UniqueBroadcastId Kodi will echo back on the PVRTimer
    // when the user schedules a recording for that EPG entry. Returns an
    // empty string if the broadcast hasn't been parsed (yet, or ever) in
    // this session, or if the backend didn't provide epg_event_id for it -
    // callers must treat that as "no listing guid available", not an error.
    static std::string GetEpgEventId(unsigned int broadcastId);

private:
    // Shared parsing logic - single source of truth for EPG JSON -> PVREPGTag mapping.
    static bool ParseEPGResponse(const std::string& response,
                                 int channelUid,
                                 const std::function<bool(const std::string&, nlohmann::json&)>& parseJson,
                                 kodi::addon::PVREPGTagsResultSet& results);

    // broadcastId -> epg_event_id (listing guid), populated in ParseEPGResponse.
    // Static/process-lifetime cache, not per-instance: TimerManager::AddTimer needs
    // to look this up independently of whatever EPGManager instance (if any) last
    // parsed the relevant channel's grid - mirroring the fact that every method on
    // this class is already static with no per-instance state.
    //
    // Unbounded growth guard: capped and cleared wholesale past
    // kMaxCacheEntries rather than evicted piecemeal (LRU bookkeeping isn't
    // worth it here) - see the cap check in ParseEPGResponse. Losing old
    // entries just means GetEpgEventId returns "" for a broadcast that was
    // only seen a long time ago, which callers already have to handle.
    static std::map<unsigned int, std::string> s_epgEventIdCache;
    static std::shared_mutex s_epgEventIdCacheMutex;
    static constexpr size_t kMaxCacheEntries = 200000;
};