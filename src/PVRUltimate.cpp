#include "PVRUltimate.h"
#include "Utils.h"
#include <kodi/General.h>
#include <kodi/AddonBase.h>
#include <kodi/vfs/File.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <thread>
#include <chrono>
#include <iomanip>

CPVRUltimate::CPVRUltimate()
    : m_backendUrl("127.0.0.1"),
      m_backendPort(7777),
      m_backendAvailable(false),
      m_maxRetries(10),
      m_retryDelayMs(2000),
      m_supportsPiggyback(false),
      m_useModernDrm(false) {
  kodi::Log(ADDON_LOG_INFO, "Ultimate PVR Client starting...");

  // Initialize managers
  m_providerManager = std::make_unique<ProviderManager>();
  m_channelManager = std::make_unique<ChannelManager>();
  m_epgManager = std::make_unique<EPGManager>();
  m_recordingManager = std::make_unique<RecordingManager>();
  m_timerManager = std::make_unique<TimerManager>();

  DetectInputstreamVersion();

  {
    std::lock_guard<std::mutex> lock(m_configMutex);
    m_backendUrl = kodi::addon::GetSettingString("backend_url", "127.0.0.1");
    m_backendPort = kodi::addon::GetSettingInt("backend_port", 7777);
  }
  m_maxRetries = kodi::addon::GetSettingInt("retry_attempts", 10);
  m_retryDelayMs = kodi::addon::GetSettingInt("retry_delay", 2000);

  if (RetryBackendCall("initialization")) {
    DetectBackendCapabilities();

    // Lambda wrappers for HTTP calls
    auto httpGet = [this](const std::string& endpoint) -> std::string {
      return this->HttpGet(this->BuildApiUrl(endpoint));
    };
    auto parseJson = [](const std::string& response, rapidjson::Document& doc) -> bool {
      return Utils::ParseJsonResponse(response, doc);
    };

    // Load all data using managers
    if (!m_providerManager->LoadProviders(httpGet, parseJson)) {
      kodi::Log(ADDON_LOG_ERROR, "Failed to load providers");
    }

    const auto& providers = m_providerManager->GetProviders();

    if (!m_channelManager->LoadChannels(providers, httpGet, parseJson)) {
      kodi::Log(ADDON_LOG_ERROR, "Failed to load channels");
    }

    if (!m_timerManager->LoadTimerTypes(providers, httpGet, parseJson)) {
      kodi::Log(ADDON_LOG_WARNING, "Failed to load timer types");
    }

    if (!m_recordingManager->LoadRecordings(providers, httpGet, parseJson)) {
      kodi::Log(ADDON_LOG_WARNING, "Failed to load recordings or none available");
    }

    if (!m_timerManager->LoadTimers(providers, httpGet, parseJson)) {
      kodi::Log(ADDON_LOG_WARNING, "Failed to load timers or none available");
    }
  }

  int channelCount = m_channelManager->GetChannelsAmount();
  int recordingCount = m_recordingManager->GetRecordingsAmount(false);
  int timerCount = m_timerManager->GetTimersAmount();

  kodi::Log(ADDON_LOG_INFO, "Ultimate PVR Client loaded %d channels, %d recordings, %d timers",
            channelCount, recordingCount, timerCount);
}

CPVRUltimate::~CPVRUltimate() {
  kodi::Log(ADDON_LOG_INFO, "Ultimate PVR Client stopping...");
}

void CPVRUltimate::DetectInputstreamVersion() {
  m_useModernDrm = false;

  std::string isaVersion;
  bool isaEnabled = false;
  if (kodi::IsAddonAvailable("inputstream.adaptive", isaVersion, isaEnabled)) {
    int isaMajor = 0;
    std::istringstream(isaVersion) >> isaMajor;
    m_useModernDrm = (isaMajor >= 20);
    kodi::Log(ADDON_LOG_INFO, "inputstream.adaptive version: %s, enabled: %s, modern DRM: %s",
              isaVersion.c_str(), isaEnabled ? "yes" : "no", m_useModernDrm.load() ? "yes" : "no");
  } else {
    kodi::Log(ADDON_LOG_WARNING,
              "inputstream.adaptive not detected, defaulting to legacy DRM properties");
  }
}

void CPVRUltimate::DetectBackendCapabilities() {
  m_supportsPiggyback = false;
  std::string url = BuildApiUrl("/api/version");
  std::string response = HttpGet(url);

  if (!response.empty()) {
    rapidjson::Document doc;
    if (Utils::ParseJsonResponse(response, doc) && doc.IsObject()) {
      if (doc.HasMember("features") && doc["features"].IsArray()) {
        for (const auto& feature : doc["features"].GetArray()) {
          if (feature.IsString() && std::string(feature.GetString()) == "header_piggyback") {
            m_supportsPiggyback = true;
            kodi::Log(ADDON_LOG_INFO, "Backend supports header piggyback");
            break;
          }
        }
      }
    }
  }
}

bool CPVRUltimate::RetryBackendCall(const std::string& operationName) {
  int maxRetries = m_maxRetries.load();
  for (int attempt = 1; attempt <= maxRetries; attempt++) {
    std::string testUrl = BuildApiUrl("/api/providers");
    std::string response = HttpGet(testUrl);
    if (!response.empty()) {
      kodi::Log(ADDON_LOG_INFO, "Backend connection established on attempt %d", attempt);
      m_backendAvailable = true;
      return true;
    }
    if (attempt < maxRetries) {
      int delay = m_retryDelayMs.load() * attempt;
      kodi::Log(ADDON_LOG_WARNING, "Backend not ready for %s, attempt %d/%d, retrying in %dms...",
                operationName.c_str(), attempt, maxRetries, delay);
      SleepMs(delay);
    }
  }
  kodi::Log(ADDON_LOG_ERROR, "Backend unavailable for %s after %d attempts",
            operationName.c_str(), maxRetries);
  m_backendAvailable = false;
  return false;
}

void CPVRUltimate::SleepMs(int milliseconds) {
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

ADDON_STATUS CPVRUltimate::SetSetting(const std::string& settingName,
                                       const kodi::addon::CSettingValue& settingValue) {
  kodi::Log(ADDON_LOG_DEBUG, "Setting changed: %s", settingName.c_str());

  if (settingName == "backend_url") {
    std::lock_guard<std::mutex> lock(m_configMutex);
    m_backendUrl = settingValue.GetString();
    kodi::Log(ADDON_LOG_INFO, "Backend URL changed to: %s", m_backendUrl.c_str());
    return ADDON_STATUS_NEED_RESTART;
  }
  else if (settingName == "backend_port") {
    std::lock_guard<std::mutex> lock(m_configMutex);
    m_backendPort = settingValue.GetInt();
    kodi::Log(ADDON_LOG_INFO, "Backend port changed to: %d", m_backendPort);
    return ADDON_STATUS_NEED_RESTART;
  }
  else if (settingName == "retry_attempts") {
    m_maxRetries = settingValue.GetInt();
    kodi::Log(ADDON_LOG_INFO, "Retry attempts changed to: %d", m_maxRetries.load());
    return ADDON_STATUS_OK;
  }
  else if (settingName == "retry_delay") {
    m_retryDelayMs = settingValue.GetInt();
    kodi::Log(ADDON_LOG_INFO, "Retry delay changed to: %dms", m_retryDelayMs.load());
    return ADDON_STATUS_OK;
  }

  return ADDON_STATUS_OK;
}

std::string CPVRUltimate::BuildApiUrl(const std::string& endpoint) {
  std::lock_guard<std::mutex> lock(m_configMutex);
  std::ostringstream url;
  // Use http for local development, can be configurable
  url << "http://" << m_backendUrl << ":" << m_backendPort << endpoint;
  return url.str();
}

std::string CPVRUltimate::HttpSendRequest(const std::string& url, const std::string& method, const std::string& body) {
  kodi::Log(ADDON_LOG_DEBUG, "HTTP %s: %s", method.c_str(), url.c_str());

  kodi::vfs::CFile file;

  if (method != "GET") {
    file.CURLOption("CUSTOMREQUEST", method.c_str());
  }

  if (!body.empty()) {
    file.CURLOption("postdata", body.c_str());
    file.SetRequestHeader("Content-Type", "application/json");
  }

  if (!file.OpenFile(url, ADDON_READ_NO_CACHE)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to open URL: %s", url.c_str());
    return "";
  }

  std::string content;
  char buffer[1024];
  ssize_t bytesRead;
  while ((bytesRead = file.Read(buffer, sizeof(buffer))) > 0) {
    content.append(buffer, bytesRead);
  }
  file.Close();
  return content;
}

std::string CPVRUltimate::HttpGet(const std::string& url) {
  return HttpSendRequest(url, "GET", "");
}

bool CPVRUltimate::HttpDelete(const std::string& url) {
  std::string resp = HttpSendRequest(url, "DELETE", "");
  if (resp.empty()) {
    kodi::Log(ADDON_LOG_ERROR, "HTTP DELETE failed: %s", url.c_str());
    return false;
  }
  return true;
}

bool CPVRUltimate::HttpPost(const std::string& url, const std::string& body) {
  std::string resp = HttpSendRequest(url, "POST", body);
  if (resp.empty()) {
    kodi::Log(ADDON_LOG_ERROR, "HTTP POST failed: %s", url.c_str());
    return false;
  }
  return true;
}

bool CPVRUltimate::HttpPut(const std::string& url, const std::string& body) {
  std::string resp = HttpSendRequest(url, "PUT", body);
  if (resp.empty()) {
    kodi::Log(ADDON_LOG_ERROR, "HTTP PUT failed: %s", url.c_str());
    return false;
  }
  return true;
}

bool CPVRUltimate::HttpGetWithHeaders(const std::string& url,
                                       std::string& response,
                                       std::string& drmConfigsBase64,
                                       std::string& streamHeadersBase64) {
  response = HttpGet(url);
  if (response.empty()) return false;

  rapidjson::Document doc;
  if (Utils::ParseJsonResponse(response, doc) && doc.IsObject()) {
    if (doc.HasMember("drm_configs_base64") && doc["drm_configs_base64"].IsString()) {
      drmConfigsBase64 = doc["drm_configs_base64"].GetString();
    }
    if (doc.HasMember("stream_headers_base64") && doc["stream_headers_base64"].IsString()) {
      streamHeadersBase64 = doc["stream_headers_base64"].GetString();
    }
    return true;
  }
  return false;
}

std::string CPVRUltimate::GetManifestUrl(const std::string& provider, const std::string& channelId) {
  return BuildApiUrl("/api/providers/" + provider + "/channels/" + channelId + "/manifest");
}

DRMConfig CPVRUltimate::GetDRMConfig(const std::string& provider, const std::string& channelId,
                                     bool isRecording) {
  DRMConfig config;
  std::string entityPath = isRecording ? "/recordings/" : "/channels/";
  std::string response = HttpGet(BuildApiUrl("/api/providers/" + provider + entityPath + channelId + "/drm"));
  if (response.empty()) return config;

  rapidjson::Document document;
  if (!Utils::ParseJsonResponse(response, document)) return config;

  if (document.HasMember("drm_configs") && document["drm_configs"].IsObject()) {
    const rapidjson::Value& drmConfigs = document["drm_configs"];

    std::vector<std::pair<std::string, int>> drmSystems;
    for (auto it = drmConfigs.MemberBegin(); it != drmConfigs.MemberEnd(); ++it) {
      int priority = 1;
      if (it->value.HasMember("priority") && it->value["priority"].IsInt()) {
        priority = it->value["priority"].GetInt();
      }
      drmSystems.emplace_back(it->name.GetString(), priority);
    }

    if (!drmSystems.empty()) {
      auto selected = std::min_element(drmSystems.begin(), drmSystems.end(),
                                       [](const auto& a, const auto& b) {
                                         return a.second < b.second;
                                       });
      config.system = selected->first;
      const rapidjson::Value& drmData = drmConfigs[selected->first];
      config.priority = selected->second;

      if (drmData.HasMember("license") && drmData["license"].IsObject()) {
        const rapidjson::Value& license = drmData["license"];
        if (license.HasMember("server_url") && license["server_url"].IsString())
          config.license.serverUrl = license["server_url"].GetString();
        if (license.HasMember("req_headers") && license["req_headers"].IsString())
          config.license.reqHeaders = license["req_headers"].GetString();
        if (license.HasMember("req_data") && license["req_data"].IsString())
          config.license.reqData = license["req_data"].GetString();
        if (license.HasMember("server_certificate") && license["server_certificate"].IsString())
          config.license.serverCertificate = license["server_certificate"].GetString();
        if (license.HasMember("wrapper") && license["wrapper"].IsString())
          config.license.wrapper = license["wrapper"].GetString();
        if (license.HasMember("unwrapper") && license["unwrapper"].IsString())
          config.license.unwrapper = license["unwrapper"].GetString();
      }
    }
  }
  return config;
}

rapidjson::Document CPVRUltimate::GetDRMConfigJson(const std::string& provider, const std::string& channelId,
                                                   bool isRecording) {
  rapidjson::Document drmConfigs(rapidjson::kObjectType);
  std::string entityPath = isRecording ? "/recordings/" : "/channels/";
  std::string response = HttpGet(BuildApiUrl("/api/providers/" + provider + entityPath + channelId + "/drm"));
  if (response.empty()) return drmConfigs;

  rapidjson::Document document;
  if (!Utils::ParseJsonResponse(response, document)) return drmConfigs;

  if (document.HasMember("drm_configs") && document["drm_configs"].IsObject()) {
    drmConfigs.CopyFrom(document["drm_configs"], drmConfigs.GetAllocator());
  }
  return drmConfigs;
}

void CPVRUltimate::ApplyDRMProperties(std::vector<kodi::addon::PVRStreamProperty>& properties,
                                      const std::string& provider, const std::string& channelId,
                                      bool useCdm, const std::string& drmConfigsBase64,
                                      bool isRecording) {
  bool drmConfigured = false;

  if (!drmConfigsBase64.empty()) {
    std::string decodedDrm = Utils::Base64Decode(drmConfigsBase64);
    if (!decodedDrm.empty()) {
      rapidjson::Document drmDoc;
      if (Utils::ParseJsonResponse(decodedDrm, drmDoc) && drmDoc.IsObject()) {
        if (m_useModernDrm.load()) {
          rapidjson::StringBuffer buffer;
          rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
          drmDoc.Accept(writer);
          properties.emplace_back("inputstream.adaptive.drm", buffer.GetString());
        } else {
          std::string legacyDrm = Utils::ConvertDrmJsonToLegacy(drmDoc);
          if (!legacyDrm.empty()) properties.emplace_back("inputstream.adaptive.drm_legacy", legacyDrm);
        }
        drmConfigured = true;
      }
    }
  }

  if (!drmConfigured && useCdm) {
    if (m_useModernDrm.load()) {
      rapidjson::Document drmConfigs = GetDRMConfigJson(provider, channelId, isRecording);
      if (!drmConfigs.ObjectEmpty()) {
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        drmConfigs.Accept(writer);
        properties.emplace_back("inputstream.adaptive.drm", buffer.GetString());
      }
    } else {
      DRMConfig drmConfig = GetDRMConfig(provider, channelId, isRecording);
      if (!drmConfig.system.empty() && !drmConfig.license.serverUrl.empty()) {
        std::string legacy = drmConfig.system + "|" + drmConfig.license.serverUrl;
        if (!drmConfig.license.reqHeaders.empty()) legacy += "|" + drmConfig.license.reqHeaders;
        if (!drmConfig.license.reqData.empty()) legacy += "|" + drmConfig.license.reqData;
        properties.emplace_back("inputstream.adaptive.drm_legacy", legacy);
      }
    }
  }
}

// ============================================================================
// PVR Capability Methods
// ============================================================================

PVR_ERROR CPVRUltimate::GetCapabilities(kodi::addon::PVRCapabilities& capabilities) {
  capabilities.SetSupportsEPG(true);
  capabilities.SetSupportsTV(true);
  capabilities.SetSupportsRadio(true);
  capabilities.SetSupportsRecordings(true);
  capabilities.SetSupportsRecordingsDelete(true);
  // Note: SetSupportsRecordingsPlay was removed in newer Kodi versions
  // Recordings play capability is implied by SetSupportsRecordings(true)
  // capabilities.SetSupportsRecordingsPlay(true); // Commented for Kodi 21+ compatibility
  capabilities.SetSupportsTimers(true);
  capabilities.SetSupportsChannelGroups(true);
  capabilities.SetSupportsProviders(true);
  capabilities.SetSupportsRecordingPlayCount(true);
  capabilities.SetSupportsLastPlayedPosition(true);
  capabilities.SetSupportsRecordingsRename(false);
  capabilities.SetSupportsRecordingsUndelete(false);
  capabilities.SetSupportsChannelScan(false);
  capabilities.SetHandlesInputStream(false);
  capabilities.SetHandlesDemuxing(false);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetBackendName(std::string& name) {
  name = "Ultimate PVR Backend";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetBackendVersion(std::string& version) {
  version = "1.0.0";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetConnectionString(std::string& connection) {
  std::lock_guard<std::mutex> lock(m_configMutex);
  connection = m_backendUrl + ":" + std::to_string(m_backendPort);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetDriveSpace(uint64_t& total, uint64_t& used) {
  total = 1024 * 1024 * 1024;
  used = 512 * 1024 * 1024;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::OnSystemWake() {
  kodi::Log(ADDON_LOG_INFO, "System woke up. Reloading PVR data...");

  auto httpGet = [this](const std::string& endpoint) -> std::string {
    return this->HttpGet(this->BuildApiUrl(endpoint));
  };
  auto parseJson = [](const std::string& response, rapidjson::Document& doc) -> bool {
    return Utils::ParseJsonResponse(response, doc);
  };

  const auto& providers = m_providerManager->GetProviders();

  m_providerManager->LoadProviders(httpGet, parseJson);
  m_timerManager->LoadTimerTypes(providers, httpGet, parseJson);
  m_channelManager->LoadChannels(providers, httpGet, parseJson);
  m_recordingManager->LoadRecordings(providers, httpGet, parseJson);
  m_timerManager->LoadTimers(providers, httpGet, parseJson);

  return PVR_ERROR_NO_ERROR;
}

// ============================================================================
// Provider Methods
// ============================================================================

PVR_ERROR CPVRUltimate::GetProvidersAmount(int& amount) {
  amount = m_providerManager->GetProvidersAmount();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetProviders(kodi::addon::PVRProvidersResultSet& results) {
  m_providerManager->GetProviders(results);
  return PVR_ERROR_NO_ERROR;
}

// ============================================================================
// Channel Methods
// ============================================================================

PVR_ERROR CPVRUltimate::GetChannelsAmount(int& amount) {
  amount = m_channelManager->GetChannelsAmount();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results) {
  m_channelManager->GetChannels(radio, results);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetChannelStreamProperties(
    const kodi::addon::PVRChannel& channel,
    PVR_SOURCE source,
    std::vector<kodi::addon::PVRStreamProperty>& properties) {

  std::string provider, channelId;
  bool useCdm = true;
  UltimateChannel ultimateChannel;

  if (!m_channelManager->GetChannelByUid(channel.GetUniqueId(), ultimateChannel)) {
    return PVR_ERROR_SERVER_ERROR;
  }
  provider = ultimateChannel.provider;
  channelId = ultimateChannel.channelId;
  useCdm = ultimateChannel.useCdm;

  if (!m_backendAvailable.load() && !RetryBackendCall("stream playback")) {
    return PVR_ERROR_SERVER_ERROR;
  }

  std::string manifestApiUrl = GetManifestUrl(provider, channelId);
  std::string response, drmConfigsBase64, streamHeadersBase64;

  if (m_supportsPiggyback.load()) {
    if (!HttpGetWithHeaders(manifestApiUrl, response, drmConfigsBase64, streamHeadersBase64)) {
      return PVR_ERROR_SERVER_ERROR;
    }
  } else {
    response = HttpGet(manifestApiUrl);
    if (response.empty()) return PVR_ERROR_SERVER_ERROR;
  }

  rapidjson::Document document;
  if (!Utils::ParseJsonResponse(response, document)) return PVR_ERROR_SERVER_ERROR;

  std::string manifestUrl;
  if (document.HasMember("manifest_url") && document["manifest_url"].IsString()) {
    manifestUrl = document["manifest_url"].GetString();
  } else return PVR_ERROR_SERVER_ERROR;

  properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.adaptive");
  properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, manifestUrl);

  ApplyDRMProperties(properties, provider, channelId, useCdm, drmConfigsBase64);

  if (!streamHeadersBase64.empty()) {
    std::string decodedHeaders = Utils::Base64Decode(streamHeadersBase64);
    if (!decodedHeaders.empty()) {
      rapidjson::Document headersDoc;
      if (Utils::ParseJsonResponse(decodedHeaders, headersDoc) && headersDoc.IsObject()) {
        if (headersDoc.HasMember("manifest") && headersDoc["manifest"].IsObject()) {
          std::string manifestHeaders;
          for (auto it = headersDoc["manifest"].MemberBegin(); it != headersDoc["manifest"].MemberEnd(); ++it) {
            if (!manifestHeaders.empty()) manifestHeaders += "&";
            manifestHeaders += it->name.GetString();
            manifestHeaders += "=";
            manifestHeaders += Utils::UrlEncode(it->value.GetString());
          }
          if (!manifestHeaders.empty()) {
            properties.emplace_back("inputstream.adaptive.manifest_headers", manifestHeaders);
          }
        }
        if (headersDoc.HasMember("segment") && headersDoc["segment"].IsObject()) {
          std::string segmentHeaders;
          for (auto it = headersDoc["segment"].MemberBegin(); it != headersDoc["segment"].MemberEnd(); ++it) {
            if (!segmentHeaders.empty()) segmentHeaders += "&";
            segmentHeaders += it->name.GetString();
            segmentHeaders += "=";
            segmentHeaders += Utils::UrlEncode(it->value.GetString());
          }
          if (!segmentHeaders.empty()) {
            properties.emplace_back("inputstream.adaptive.stream_headers", segmentHeaders);
          }
        }
      }
    }
  }
  return PVR_ERROR_NO_ERROR;
}

// ============================================================================
// Channel Group Methods
// ============================================================================

PVR_ERROR CPVRUltimate::GetChannelGroupsAmount(int& amount) {
  amount = 2;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetChannelGroups(bool radio, kodi::addon::PVRChannelGroupsResultSet& results) {
  if (radio) {
    kodi::addon::PVRChannelGroup radioGroup;
    radioGroup.SetIsRadio(true);
    radioGroup.SetGroupName("Radio Stations");
    results.Add(radioGroup);
  } else {
    kodi::addon::PVRChannelGroup tvGroup;
    tvGroup.SetIsRadio(false);
    tvGroup.SetGroupName("TV Channels");
    results.Add(tvGroup);
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetChannelGroupMembers(
    const kodi::addon::PVRChannelGroup& group,
    kodi::addon::PVRChannelGroupMembersResultSet& results) {

  bool isRadioGroup = group.GetIsRadio();
  std::string groupName = group.GetGroupName();

  const auto& channels = m_channelManager->GetChannels();

  for (const auto& channel : channels) {
    if (channel.isRadio == isRadioGroup) {
      kodi::addon::PVRChannelGroupMember member;
      member.SetGroupName(groupName);
      member.SetChannelUniqueId(channel.channelNumber);
      member.SetChannelNumber(channel.channelNumber);
      results.Add(member);
    }
  }
  return PVR_ERROR_NO_ERROR;
}

// ============================================================================
// EPG Methods
// ============================================================================

PVR_ERROR CPVRUltimate::GetEPGForChannel(int channelUid, time_t start, time_t end,
                                         kodi::addon::PVREPGTagsResultSet& results) {

  auto httpGet = [this](const std::string& endpoint) -> std::string {
    return this->HttpGet(this->BuildApiUrl(endpoint));
  };
  auto parseJson = [](const std::string& response, rapidjson::Document& doc) -> bool {
    return Utils::ParseJsonResponse(response, doc);
  };
  auto getChannelByUid = [this](int uid, UltimateChannel& channel) -> bool {
    return m_channelManager->GetChannelByUid(uid, channel);
  };

  m_epgManager->GetEPGForChannel(channelUid, start, end, httpGet, parseJson, getChannelByUid, results);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::IsEPGTagRecordable(const kodi::addon::PVREPGTag& tag, bool& bIsRecordable) {
  m_epgManager->IsEPGTagRecordable(tag, bIsRecordable);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& bIsPlayable) {
  auto getChannelInfo = [this](int uid, std::string& provider, std::string& channelId, int& catchupHours) -> bool {
    return m_channelManager->GetChannelInfo(uid, provider, channelId, catchupHours);
  };

  m_epgManager->IsEPGTagPlayable(tag, bIsPlayable, getChannelInfo);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetEPGTagStreamProperties(
    const kodi::addon::PVREPGTag& tag,
    std::vector<kodi::addon::PVRStreamProperty>& properties) {

  auto httpGet = [this](const std::string& endpoint) -> std::string {
    return this->HttpGet(this->BuildApiUrl(endpoint));
  };
  auto parseJson = [](const std::string& response, rapidjson::Document& doc) -> bool {
    return Utils::ParseJsonResponse(response, doc);
  };
  auto getChannelInfo = [this](int uid, std::string& provider, std::string& channelId, int& catchupHours) -> bool {
    return m_channelManager->GetChannelInfo(uid, provider, channelId, catchupHours);
  };
  auto getChannelByUid = [this](int uid, UltimateChannel& channel) -> bool {
    return m_channelManager->GetChannelByUid(uid, channel);
  };
  auto isBackendAvailable = [this]() -> bool {
    return m_backendAvailable.load();
  };
  auto retryBackendCall = [this](const std::string& op) -> bool {
    return this->RetryBackendCall(op);
  };

  bool result = m_epgManager->GetEPGTagStreamProperties(tag, properties, httpGet, parseJson,
                                                         getChannelInfo, getChannelByUid,
                                                         isBackendAvailable, retryBackendCall);

  if (!result) return PVR_ERROR_SERVER_ERROR;

  // Apply DRM properties if needed
  int channelUid = tag.GetUniqueChannelId();
  UltimateChannel channel;
  if (m_channelManager->GetChannelByUid(channelUid, channel)) {
    ApplyDRMProperties(properties, channel.provider, channel.channelId, channel.useCdm, "");
  }

  return PVR_ERROR_NO_ERROR;
}

// ============================================================================
// Recording Methods
// ============================================================================

PVR_ERROR CPVRUltimate::GetRecordingsAmount(bool deleted, int& amount) {
  amount = m_recordingManager->GetRecordingsAmount(deleted);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results) {
  m_recordingManager->GetRecordings(deleted, results);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::DeleteRecording(const kodi::addon::PVRRecording& recording) {
  std::string recordingId = recording.GetRecordingId();

  auto buildApiUrl = [this](const std::string& endpoint) -> std::string {
    return this->BuildApiUrl(endpoint);
  };
  auto httpDelete = [this](const std::string& url) -> bool {
    return this->HttpDelete(url);
  };

  if (!m_recordingManager->DeleteRecording(recordingId, buildApiUrl, httpDelete)) {
    return PVR_ERROR_SERVER_ERROR;
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetRecordingStreamProperties(
    const kodi::addon::PVRRecording& recording,
    std::vector<kodi::addon::PVRStreamProperty>& properties) {

  std::string recordingId = recording.GetRecordingId();

  auto buildApiUrl = [this](const std::string& endpoint) -> std::string {
    return this->BuildApiUrl(endpoint);
  };
  auto httpGet = [this](const std::string& url) -> std::string {
    return this->HttpGet(url);
  };
  auto parseJson = [](const std::string& response, rapidjson::Document& doc) -> bool {
    return Utils::ParseJsonResponse(response, doc);
  };
  auto httpGetWithHeaders = [this](const std::string& url, std::string& response,
                                    std::string& drmConfigs, std::string& headers) -> bool {
    return this->HttpGetWithHeaders(url, response, drmConfigs, headers);
  };

  bool result = m_recordingManager->GetRecordingStreamProperties(recordingId, properties,
                                                                  buildApiUrl, httpGet, parseJson,
                                                                  httpGetWithHeaders,
                                                                  m_supportsPiggyback.load());

  if (!result) return PVR_ERROR_SERVER_ERROR;

  // Apply DRM properties if needed. Recordings are DRM-looked-up via the
  // /recordings/ endpoint (not /channels/), since rec->uniqueId is a
  // recording id, not a channel id.
  if (auto* rec = m_recordingManager->FindRecording(recordingId)) {
    ApplyDRMProperties(properties, rec->provider, rec->uniqueId, true, "", /*isRecording=*/true);
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetRecordingEdl(const kodi::addon::PVRRecording& recording,
                                       std::vector<kodi::addon::PVREDLEntry>& edl) {
  return PVR_ERROR_NOT_IMPLEMENTED;
}

// ============================================================================
// Timer Methods
// ============================================================================

PVR_ERROR CPVRUltimate::GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types) {
  m_timerManager->GetTimerTypes(types);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetTimersAmount(int& amount) {
  amount = m_timerManager->GetTimersAmount();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetTimers(kodi::addon::PVRTimersResultSet& results) {
  m_timerManager->GetTimers(results);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::AddTimer(const kodi::addon::PVRTimer& timer) {
  auto buildApiUrl = [this](const std::string& endpoint) -> std::string {
    return this->BuildApiUrl(endpoint);
  };
  auto httpPost = [this](const std::string& url, const std::string& body) -> bool {
    return this->HttpPost(url, body);
  };
  auto loadTimers = [this]() {
    auto httpGet = [this](const std::string& endpoint) -> std::string {
      return this->HttpGet(this->BuildApiUrl(endpoint));
    };
    auto parseJson = [](const std::string& response, rapidjson::Document& doc) -> bool {
      return Utils::ParseJsonResponse(response, doc);
    };
    const auto& providers = m_providerManager->GetProviders();
    m_timerManager->LoadTimers(providers, httpGet, parseJson);
  };

  if (!m_timerManager->AddTimer(timer, m_providerManager->GetProviders(),
                                m_channelManager->GetLookup(),
                                buildApiUrl, httpPost, loadTimers)) {
    return PVR_ERROR_SERVER_ERROR;
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete) {
  int clientIndex = timer.GetClientIndex();

  auto buildApiUrl = [this](const std::string& endpoint) -> std::string {
    return this->BuildApiUrl(endpoint);
  };
  auto httpDelete = [this](const std::string& url) -> bool {
    return this->HttpDelete(url);
  };
  auto loadTimers = [this]() {
    auto httpGet = [this](const std::string& endpoint) -> std::string {
      return this->HttpGet(this->BuildApiUrl(endpoint));
    };
    auto parseJson = [](const std::string& response, rapidjson::Document& doc) -> bool {
      return Utils::ParseJsonResponse(response, doc);
    };
    const auto& providers = m_providerManager->GetProviders();
    m_timerManager->LoadTimers(providers, httpGet, parseJson);
  };

  if (!m_timerManager->DeleteTimer(clientIndex, forceDelete, buildApiUrl, httpDelete, loadTimers)) {
    return PVR_ERROR_SERVER_ERROR;
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::UpdateTimer(const kodi::addon::PVRTimer& timer) {
  auto buildApiUrl = [this](const std::string& endpoint) -> std::string {
    return this->BuildApiUrl(endpoint);
  };
  auto httpPut = [this](const std::string& url, const std::string& body) -> bool {
    return this->HttpPut(url, body);
  };
  auto loadTimers = [this]() {
    auto httpGet = [this](const std::string& endpoint) -> std::string {
      return this->HttpGet(this->BuildApiUrl(endpoint));
    };
    auto parseJson = [](const std::string& response, rapidjson::Document& doc) -> bool {
      return Utils::ParseJsonResponse(response, doc);
    };
    const auto& providers = m_providerManager->GetProviders();
    m_timerManager->LoadTimers(providers, httpGet, parseJson);
  };

  if (!m_timerManager->UpdateTimer(timer, buildApiUrl, httpPut, loadTimers)) {
    return PVR_ERROR_SERVER_ERROR;
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& signalStatus) {
  signalStatus.SetAdapterName("Ultimate PVR");
  signalStatus.SetAdapterStatus(m_backendAvailable.load() ? "Connected" : "Disconnected");
  return PVR_ERROR_NO_ERROR;
}

ADDONCREATOR(CPVRUltimate)