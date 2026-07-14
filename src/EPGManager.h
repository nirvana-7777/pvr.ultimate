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

    bool GetEPGForChannel(int channelUid, time_t start, time_t end,
                          const std::function<std::string(const std::string&)>& httpGet,
                          const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson,
                          const std::function<bool(int, UltimateChannel&)>& getChannelByUid,
                          kodi::addon::PVREPGTagsResultSet& results);

    bool IsEPGTagRecordable(const kodi::addon::PVREPGTag& tag, bool& isRecordable);
    bool IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& isPlayable,
                          const std::function<bool(int, std::string&, std::string&, int&)>& getChannelInfo);

    bool GetEPGTagStreamProperties(const kodi::addon::PVREPGTag& tag,
                                   std::vector<kodi::addon::PVRStreamProperty>& properties,
                                   const std::function<std::string(const std::string&)>& httpGet,
                                   const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson,
                                   const std::function<bool(int, std::string&, std::string&, int&)>& getChannelInfo,
                                   const std::function<bool(int, UltimateChannel&)>& getChannelByUid,
                                   const std::function<bool()>& isBackendAvailable,
                                   const std::function<bool(const std::string&)>& retryBackendCall);
};