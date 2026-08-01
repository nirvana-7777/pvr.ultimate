#pragma once

#include "Models.h"
#include <kodi/addon-instance/PVR.h>
#include <vector>
#include <map>
#include <unordered_map>
#include <shared_mutex>
#include <functional>
#include <string>
#include <nlohmann/json.hpp>

class ChannelManager {
public:
    static constexpr int PROVIDER_OFFSET_MULTIPLIER = 100000;

    ChannelManager() = default;

    bool LoadChannels(const std::vector<UltimateProvider>& providers,
                      const std::function<std::string(const std::string&)>& httpGet,
                      const std::function<bool(const std::string&, nlohmann::json&)>& parseJson);

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
    static void LoadChannelsForProvider(const std::string& provider, int providerIndex,
                                        const std::function<std::string(const std::string&)>& httpGet,
                                        const std::function<bool(const std::string&, nlohmann::json&)>& parseJson,
                                        std::vector<UltimateChannel>& outChannels,
                                        std::map<int, ChannelLookupInfo>& outLookup);

    std::vector<UltimateChannel> m_channels;
    std::map<int, ChannelLookupInfo> m_channelLookup;
    std::unordered_map<int, size_t> m_channelIndex;  // channelNumber -> index into m_channels, O(1) GetChannelByUid
    mutable std::shared_mutex m_dataMutex;
};