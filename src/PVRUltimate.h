#pragma once

#include <kodi/addon-instance/PVR.h>
#include "Models.h"
#include "ProviderManager.h"
#include "ChannelManager.h"
#include "EPGManager.h"
#include "RecordingManager.h"
#include "TimerManager.h"
#include <memory>
#include <atomic>
#include <mutex>

// Forward declare rapidjson types to avoid including in header
#include "rapidjson/document.h"

class ATTR_DLL_LOCAL CPVRUltimate : public kodi::addon::CAddonBase,
                                     public kodi::addon::CInstancePVRClient {
public:
  CPVRUltimate();
  ~CPVRUltimate() override;

  ADDON_STATUS SetSetting(const std::string& settingName,
                         const kodi::addon::CSettingValue& settingValue) override;

  PVR_ERROR GetCapabilities(kodi::addon::PVRCapabilities& capabilities) override;
  PVR_ERROR GetBackendName(std::string& name) override;
  PVR_ERROR GetBackendVersion(std::string& version) override;
  PVR_ERROR GetConnectionString(std::string& connection) override;
  PVR_ERROR GetDriveSpace(uint64_t& total, uint64_t& used) override;

  PVR_ERROR GetProvidersAmount(int& amount) override;
  PVR_ERROR GetProviders(kodi::addon::PVRProvidersResultSet& results) override;

    PVR_ERROR GetChannelsAmount(int& amount) override;
    PVR_ERROR GetChannels(bool bRadio, kodi::addon::PVRChannelsResultSet& results) override;
#ifdef ULTIMATE_HAS_PVR_SOURCE
    PVR_ERROR GetChannelStreamProperties(
        const kodi::addon::PVRChannel& channel,
        PVR_SOURCE source,
        std::vector<kodi::addon::PVRStreamProperty>& properties) override;
#else
    PVR_ERROR GetChannelStreamProperties(
        const kodi::addon::PVRChannel& channel,
        std::vector<kodi::addon::PVRStreamProperty>& properties) override;
#endif

  PVR_ERROR GetChannelGroupsAmount(int& amount) override;
  PVR_ERROR GetChannelGroups(bool bRadio, kodi::addon::PVRChannelGroupsResultSet& results) override;
  PVR_ERROR GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group,
                                  kodi::addon::PVRChannelGroupMembersResultSet& results) override;

  PVR_ERROR GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& signalStatus) override;

  PVR_ERROR GetEPGForChannel(int channelUid, time_t start, time_t end,
                            kodi::addon::PVREPGTagsResultSet& results) override;
  PVR_ERROR IsEPGTagRecordable(const kodi::addon::PVREPGTag& tag, bool& bIsRecordable) override;
  PVR_ERROR IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& bIsPlayable) override;
  PVR_ERROR GetEPGTagStreamProperties(const kodi::addon::PVREPGTag& tag,
                                     std::vector<kodi::addon::PVRStreamProperty>& properties) override;

  PVR_ERROR GetRecordingsAmount(bool deleted, int& amount) override;
  PVR_ERROR GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results) override;
  PVR_ERROR DeleteRecording(const kodi::addon::PVRRecording& recording) override;
  PVR_ERROR GetRecordingStreamProperties(
      const kodi::addon::PVRRecording& recording,
      std::vector<kodi::addon::PVRStreamProperty>& properties) override;
  PVR_ERROR GetRecordingEdl(const kodi::addon::PVRRecording& recording,
                           std::vector<kodi::addon::PVREDLEntry>& edl) override;

  PVR_ERROR GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types) override;
  PVR_ERROR GetTimersAmount(int& amount) override;
  PVR_ERROR GetTimers(kodi::addon::PVRTimersResultSet& results) override;
  PVR_ERROR AddTimer(const kodi::addon::PVRTimer& timer) override;
  PVR_ERROR DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete) override;
  PVR_ERROR UpdateTimer(const kodi::addon::PVRTimer& timer) override;

  PVR_ERROR OnSystemWake() override;

private:
  // Configuration
  std::string m_backendUrl;
  int m_backendPort;
  std::mutex m_configMutex;

  std::atomic<bool> m_backendAvailable;
  std::atomic<int> m_maxRetries;
  std::atomic<int> m_retryDelayMs;
  std::atomic<bool> m_supportsPiggyback;
  std::atomic<bool> m_useModernDrm;

  // EPG service settings (database EPG service, optional alternative to backend API)
  // m_epgServiceUrl is guarded by m_configMutex, same as m_backendUrl.
  std::atomic<bool> m_useDatabaseEpg;
  std::string m_epgServiceUrl;

  // Managers
  std::unique_ptr<ProviderManager> m_providerManager;
  std::unique_ptr<ChannelManager> m_channelManager;
  std::unique_ptr<EPGManager> m_epgManager;
  std::unique_ptr<RecordingManager> m_recordingManager;
  std::unique_ptr<TimerManager> m_timerManager;

  // HTTP methods
  static std::string HttpGet(const std::string& url);

  static bool HttpGetWithHeaders(const std::string& url,
                                 std::string& response,
                                 std::string& drmConfigsBase64,
                                 std::string& streamHeadersBase64);

  static bool HttpDelete(const std::string& url);

  static bool HttpPost(const std::string& url, const std::string& body);

  static bool HttpPut(const std::string& url, const std::string& body);
  static std::string HttpSendRequest(const std::string& url, const std::string& method, const std::string& body);

  // Core methods
  bool RetryBackendCall(const std::string& operationName);

  static void SleepMs(int milliseconds);
  void DetectInputstreamVersion();
  void DetectBackendCapabilities();
  std::string BuildApiUrl(const std::string& endpoint);

  // DRM methods
  DRMConfig GetDRMConfig(const std::string& provider, const std::string& channelId,
                        bool isRecording = false);
  rapidjson::Document GetDRMConfigJson(const std::string& provider, const std::string& channelId,
                                       bool isRecording = false);
  std::string GetManifestUrl(const std::string& provider, const std::string& channelId);
  void ApplyDRMProperties(std::vector<kodi::addon::PVRStreamProperty>& properties,
                          const std::string& provider, const std::string& channelId,
                          bool useCdm, const std::string& drmConfigsBase64,
                          bool isRecording = false);
};