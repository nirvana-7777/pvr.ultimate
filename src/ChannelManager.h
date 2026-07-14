#pragma once

#include "Models.h"
#include <kodi/addon-instance/PVR.h>
#include <vector>
#include <map>
#include <shared_mutex>
#include <functional>
#include <string>
#include "rapidjson/document.h"

class ChannelManager {
public:
    static constexpr int PROVIDER_OFFSET_MULTIPLIER = 100000;

    ChannelManager() = default;

    bool LoadChannels(const std::vector<UltimateProvider>& providers,
                      const std::function<std::string(const std::string&)>& httpGet,
                      const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson);

    int GetChannelsAmount() const;
    bool GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results) const;
    bool GetChannelInfo(int channelUid, std::string& provider, std::string& channelId, int& catchupHours) const;
    bool GetChannelByUid(int channelUid, UltimateChannel& channel) const;

    const std::vector<UltimateChannel>& GetChannels() const { return m_channels; }
    const std::map<int, ChannelLookupInfo>& GetLookup() const { return m_channelLookup; }

    void LockShared() const { m_dataMutex.lock_shared(); }
    void UnlockShared() const { m_dataMutex.unlock_shared(); }
    void LockUnique() const { m_dataMutex.lock(); }
    void UnlockUnique() const { m_dataMutex.unlock(); }

private:
    void LoadChannelsForProvider(const std::string& provider, int providerIndex,
                                 const std::function<std::string(const std::string&)>& httpGet,
                                 const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson,
                                 std::vector<UltimateChannel>& outChannels,
                                 std::map<int, ChannelLookupInfo>& outLookup);

    std::vector<UltimateChannel> m_channels;
    std::map<int, ChannelLookupInfo> m_channelLookup;
    mutable std::shared_mutex m_dataMutex;
};