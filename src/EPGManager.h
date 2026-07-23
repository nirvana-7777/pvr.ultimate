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

    static bool GetEPGTagStreamProperties(const kodi::addon::PVREPGTag& tag,
                                          std::vector<kodi::addon::PVRStreamProperty>& properties,
                                          const std::function<std::string(const std::string&)>& httpGet,
                                          const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson,
                                          const std::function<bool(int, std::string&, std::string&, int&)>& getChannelInfo,
                                          const std::function<bool(int, UltimateChannel&)>& getChannelByUid,
                                          const std::function<bool()>& isBackendAvailable,
                                          const std::function<bool(const std::string&)>& retryBackendCall);

private:
    // Shared parsing logic - single source of truth for EPG JSON -> PVREPGTag mapping.
    static bool ParseEPGResponse(const std::string& response,
                                 int channelUid,
                                 const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson,
                                 kodi::addon::PVREPGTagsResultSet& results);
};