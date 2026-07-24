#pragma once

#include "Models.h"
#include <kodi/addon-instance/PVR.h>
#include <vector>
#include <functional>
#include <string>
#include "rapidjson/document.h"

class EPGManager {
public:
    EPGManager() = default;

    static bool GetEPGForChannel(int channelUid, time_t start, time_t end,
                                 const std::function<std::string(const std::string&)>& httpGet,
                                 const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson,
                                 const std::function<bool(int, UltimateChannel&)>& getChannelByUid,
                                 kodi::addon::PVREPGTagsResultSet& results);

    // Extended overload: optionally try a database EPG service first via httpGetAbsolute,
    // falling back to the backend API (httpGet) on empty response or parse failure.
    static bool GetEPGForChannel(int channelUid, time_t start, time_t end,
                                 const std::function<std::string(const std::string&)>& httpGet,
                                 const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson,
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
                                          const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson,
                                          const std::function<bool(int, std::string&, std::string&, int&)>& getChannelInfo,
                                          const std::function<bool(int, UltimateChannel&)>& getChannelByUid,
                                          const std::function<bool()>& isBackendAvailable,
                                          const std::function<bool(const std::string&)>& retryBackendCall,
                                          const std::function<std::string(const std::string&, const std::string&)>& getManifestUrl,
                                          const std::function<bool(const std::string&, std::string&, std::string&, std::string&)>& httpGetWithHeaders,
                                          bool supportsPiggyback,
                                          std::string& drmConfigsBase64,
                                          std::string& streamHeadersBase64);

private:
    // Shared parsing logic - single source of truth for EPG JSON -> PVREPGTag mapping.
    static bool ParseEPGResponse(const std::string& response,
                                 int channelUid,
                                 const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson,
                                 kodi::addon::PVREPGTagsResultSet& results);
};