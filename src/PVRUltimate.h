#pragma once

#include <kodi/addon-instance/PVR.h>
#include <kodi/Filesystem.h>
#include <string>
#include <vector>
#include <map>
#include <memory>

// Replace JsonCpp with RapidJSON
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

struct UltimateProvider {
  std::string name;
  std::string label;
  std::string country;
  std::string logo;
  bool enabled;
  int uniqueId;
};

struct UltimateChannel {
  std::string uniqueId;
  int channelNumber;
  std::string channelName;
  std::string iconPath;
  std::string provider;
  std::string channelId;
  bool isRadio;

  // Stream properties
  std::string mode;
  bool sessionManifest;
  std::string manifest;
  std::string manifestScript;
  bool useCdm;
  std::string cdmMode;
  std::string contentType;
  std::string country;
  std::string language;
  std::string streamingFormat;
};

struct DRMLicense {
  std::string serverUrl;
  std::string serverCertificate;
  std::string reqHeaders;
  std::string reqData;
  std::string reqParams;
  bool useHttpGetRequest;
  std::string wrapper;
  std::string unwrapper;
};

struct DRMConfig {
  std::string system;
  int priority;
  DRMLicense license;
};

struct ChannelLookupInfo {
  std::string provider;
  std::string channelId;
  int catchupHours;  // CHANGE from catchupDays
};

class ATTR_DLL_LOCAL CPVRUltimate : public kodi::addon::CAddonBase,
                                     public kodi::addon::CInstancePVRClient {
public:
  CPVRUltimate();
  ~CPVRUltimate() override;

  // Addon instance handling
  ADDON_STATUS SetSetting(const std::string& settingName,
                         const kodi::addon::CSettingValue& settingValue) override;

  // PVR Capabilities
  PVR_ERROR GetCapabilities(kodi::addon::PVRCapabilities& capabilities) override;
  PVR_ERROR GetBackendName(std::string& name) override;
  PVR_ERROR GetBackendVersion(std::string& version) override;
  PVR_ERROR GetConnectionString(std::string& connection) override;

  // Provider handling
  PVR_ERROR GetProvidersAmount(int& amount) override;
  PVR_ERROR GetProviders(kodi::addon::PVRProvidersResultSet& results) override;

  // Channel handling
  PVR_ERROR GetChannelsAmount(int& amount) override;
  PVR_ERROR GetChannels(bool bRadio, kodi::addon::PVRChannelsResultSet& results) override;
  PVR_ERROR GetChannelStreamProperties(
      const kodi::addon::PVRChannel& channel,
      PVR_SOURCE source,
      std::vector<kodi::addon::PVRStreamProperty>& properties) override;

  // Channel groups
  PVR_ERROR GetChannelGroupsAmount(int& amount) override;
  PVR_ERROR GetChannelGroups(bool bRadio, kodi::addon::PVRChannelGroupsResultSet& results) override;
  PVR_ERROR GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group,
                                  kodi::addon::PVRChannelGroupMembersResultSet& results) override;

  // Signal status (for debugging)
  PVR_ERROR GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& signalStatus) override;

  // EPG methods
  PVR_ERROR GetEPGForChannel(int channelUid, time_t start, time_t end,
                            kodi::addon::PVREPGTagsResultSet& results) override;
  PVR_ERROR IsEPGTagRecordable(const kodi::addon::PVREPGTag& tag, bool& bIsRecordable) override;
  PVR_ERROR IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& bIsPlayable) override;
  PVR_ERROR GetEPGTagStreamProperties(const kodi::addon::PVREPGTag& tag,
                                     std::vector<kodi::addon::PVRStreamProperty>& properties) override;

private:
  // Configuration
  std::string m_backendUrl;
  int m_backendPort;
  bool m_backendAvailable;
  int m_retryCount;

  // Inputstream.adaptive version detection
  bool m_useModernDrm;

  // Retry constants
  static const int MAX_RETRIES = 10;
  static const int RETRY_DELAY_MS = 2000;

  // Data
  std::vector<UltimateProvider> m_providers;
  std::vector<UltimateChannel> m_channels;
  int m_nextChannelNumber;
  std::map<std::string, int> m_providerIdMap;
  std::map<int, ChannelLookupInfo> m_channelLookup;

  // Helper methods
  bool RetryBackendCall(const std::string& operationName);
  void SleepMs(int milliseconds);
  bool LoadProviders();
  bool LoadChannels();
  bool LoadChannelsForProvider(const std::string& provider);
  DRMConfig GetDRMConfig(const std::string& provider, const std::string& channelId);
  rapidjson::Document GetDRMConfigJson(const std::string& provider, const std::string& channelId);
  std::string GetManifestUrl(const std::string& provider, const std::string& channelId);
  bool IsProviderEnabled(const std::string& provider);
  int GenerateProviderUniqueId(const std::string& providerName);
  bool GetChannelInfo(int channelUid, std::string& provider, std::string& channelId, int& catchupDays);


  // Version detection
  void DetectInputstreamVersion();

  // HTTP helpers
  std::string HttpGet(const std::string& url);
  bool ParseJsonResponse(const std::string& response, rapidjson::Document& document);

  // URL building
  std::string BuildApiUrl(const std::string& endpoint);
};