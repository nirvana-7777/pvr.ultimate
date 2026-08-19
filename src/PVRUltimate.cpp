#include "PVRUltimate.h"
#include "Utils.h"
#include <kodi/General.h>
#include <kodi/AddonBase.h>
#include <kodi/Filesystem.h>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <thread>
#include <chrono>

CPVRUltimate::CPVRUltimate()
    : m_backendUrl("127.0.0.1"),
      m_backendPort(7777),
      m_backendAvailable(false),
      m_maxRetries(10),
      m_retryDelayMs(2000),
      m_supportsPiggyback(false),
      m_useModernDrm(false),
      m_useDatabaseEpg(false),
      m_epgServiceUrl("http://localhost:8080") {
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
    m_apiKey = kodi::addon::GetSettingString("api_key", "");
    m_customHeaders = kodi::addon::GetSettingString("custom_headers", "");
    m_epgServiceUrl = kodi::addon::GetSettingString("epg_service_url", "http://localhost:8080");
  }
  m_maxRetries = kodi::addon::GetSettingInt("retry_attempts", 10);
  m_retryDelayMs = kodi::addon::GetSettingInt("retry_delay", 2000);
  m_useDatabaseEpg = kodi::addon::GetSettingBoolean("epg_enabled", false);

  // Backend discovery and all initial data loading happen on a background
  // thread (see InitializeAsync) rather than here, so a slow or unreachable
  // backend cannot block Kodi's PVR-client construction. With
  // m_maxRetries=10 and exponential-ish backoff (retryDelayMs * attempt),
  // doing this synchronously in the constructor could block for close to two
  // minutes, which risks Kodi's own watchdog marking the addon unresponsive.
  m_initThread = std::thread(&CPVRUltimate::InitializeAsync, this);
}

CPVRUltimate::~CPVRUltimate() {
  kodi::Log(ADDON_LOG_INFO, "Ultimate PVR Client stopping...");
  EnsureInitThreadStopped();
  if (m_initThread.joinable()) {
    m_initThread.join();
  }
}

void CPVRUltimate::EnsureInitThreadStopped() {
  m_stopInit = true;
  m_initCv.notify_all();

  std::unique_lock<std::mutex> lock(m_initMutex);
  m_initCv.wait_for(lock, std::chrono::seconds(15), [this]() { return !m_initRunning.load(); });
}

void CPVRUltimate::InitializeAsync() {
  m_stopInit = false;
  m_initRunning = true;
  kodi::Log(ADDON_LOG_INFO, "Background initialization started...");

  try {
    if (RetryBackendCall("initialization")) {
      if (m_stopInit.load()) {
        m_initRunning = false;
        m_initCv.notify_all();
        return;
      }

      DetectBackendCapabilities();

      auto httpGet = [this](const std::string& endpoint) -> std::string {
        return this->HttpGet(this->BuildApiUrl(endpoint));
      };
      auto parseJson = [](const std::string& response, nlohmann::json& doc) -> bool {
        return Utils::ParseJsonResponse(response, doc);
      };

      if (m_stopInit.load()) { m_initRunning = false; m_initCv.notify_all(); return; }
      if (!m_providerManager->LoadProviders(httpGet, parseJson)) {
        kodi::Log(ADDON_LOG_ERROR, "Failed to load providers");
      }

      const auto& providers = m_providerManager->GetProviders();

      if (m_stopInit.load()) { m_initRunning = false; m_initCv.notify_all(); return; }
      if (!m_channelManager->LoadChannels(providers, httpGet, parseJson)) {
        kodi::Log(ADDON_LOG_ERROR, "Failed to load channels");
      }

      if (m_stopInit.load()) { m_initRunning = false; m_initCv.notify_all(); return; }
      if (!m_timerManager->LoadTimerTypes(providers, httpGet, parseJson)) {
        kodi::Log(ADDON_LOG_WARNING, "Failed to load timer types");
      }

      if (m_stopInit.load()) { m_initRunning = false; m_initCv.notify_all(); return; }
      if (!m_recordingManager->LoadRecordings(providers, httpGet, parseJson)) {
        kodi::Log(ADDON_LOG_WARNING, "Failed to load recordings or none available");
      }

      if (m_stopInit.load()) { m_initRunning = false; m_initCv.notify_all(); return; }
      if (!m_timerManager->LoadTimers(providers, httpGet, parseJson)) {
        kodi::Log(ADDON_LOG_WARNING, "Failed to load timers or none available");
      }
    } else {
      kodi::QueueNotification(QUEUE_WARNING, "PVR Ultimate", "Backend unavailable - check connection settings");
    }
  } catch (const std::exception& e) {
    kodi::Log(ADDON_LOG_ERROR, "Background initialization failed: %s", e.what());
    kodi::QueueNotification(QUEUE_ERROR, "PVR Ultimate", std::string("Initialization failed: ") + e.what());
  } catch (...) {
    kodi::Log(ADDON_LOG_ERROR, "Background initialization failed: unknown exception");
    kodi::QueueNotification(QUEUE_ERROR, "PVR Ultimate", "Initialization failed");
  }

  int channelCount = m_channelManager->GetChannelsAmount();
  int recordingCount = m_recordingManager->GetRecordingsAmount(false);
  int timerCount = m_timerManager->GetTimersAmount();
  kodi::Log(ADDON_LOG_INFO, "Ultimate PVR Client loaded %d channels, %d recordings, %d timers",
            channelCount, recordingCount, timerCount);

  m_initialized = true;
  m_initRunning = false;
  m_initCv.notify_all();

  // Kodi's initial PVR import runs concurrently with this background load
  // (that's the whole point of doing this off the constructor thread), so
  // it will very likely call GetChannels()/GetProviders()/etc. before
  // m_initialized flips true above, get an empty-but-successful result via
  // IsReady() gating, and consider its initial import complete. Nothing
  // else tells Kodi to re-check afterwards - these Trigger*Update() calls
  // are what asks Kodi to re-fetch now that data actually exists. Skipped
  // entirely if init was cancelled (stop/shutdown/OnSystemWake reload) so
  // a torn-down instance doesn't fire callbacks into a dead PVR manager..
  if (!m_stopInit.load()) {
    TriggerChannelUpdate();
    TriggerChannelGroupsUpdate();
    TriggerProvidersUpdate();
    TriggerRecordingUpdate();
    TriggerTimerUpdate();
  }
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
  // The backend (Bottle) has no /api/version endpoint - confirmed 404 - so
  // there is nothing to probe here. The backend is confirmed to support
  // piggybacking DRM configs / stream headers into manifest responses
  // (drm_configs_base64 / stream_headers_base64 fields). HttpGetWithHeaders
  // degrades safely to the non-piggyback path if a given response omits
  // those fields, so hardcoding this is safe even for responses that don't
  // include them.
  m_supportsPiggyback = true;
  kodi::Log(ADDON_LOG_INFO, "Header piggyback enabled (hardcoded, backend confirmed supported)");
}

bool CPVRUltimate::RetryBackendCall(const std::string& operationName) {
  // HttpGet now retries internally (see HttpGet), so this no longer loops
  // itself - doing so would multiply attempts to maxRetries^2 and stack two
  // independent backoff delays on top of each other.
  std::string testUrl = BuildApiUrl("/api/providers");
  std::string response = HttpGet(testUrl);
  if (!response.empty()) {
    kodi::Log(ADDON_LOG_INFO, "Backend connection established for %s", operationName.c_str());
    m_backendAvailable = true;
    return true;
  }
  kodi::Log(ADDON_LOG_ERROR, "Backend unavailable for %s after %d attempts",
            operationName.c_str(), m_maxRetries.load() + 1);
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
  else if (settingName == "epg_enabled") {
    m_useDatabaseEpg = settingValue.GetBoolean();
    kodi::Log(ADDON_LOG_INFO, "Database EPG service enabled: %s", m_useDatabaseEpg.load() ? "true" : "false");
    return ADDON_STATUS_OK;
  }
  else if (settingName == "api_key") {
    std::lock_guard<std::mutex> lock(m_configMutex);
    m_apiKey = settingValue.GetString();
    kodi::Log(ADDON_LOG_INFO, "API key changed");
    return ADDON_STATUS_NEED_RESTART;
  }
  else if (settingName == "custom_headers") {
    std::lock_guard<std::mutex> lock(m_configMutex);
    m_customHeaders = settingValue.GetString();
    kodi::Log(ADDON_LOG_INFO, "Custom headers changed");
    return ADDON_STATUS_NEED_RESTART;
  }
  else if (settingName == "epg_service_url") {
    std::lock_guard<std::mutex> lock(m_configMutex);
    m_epgServiceUrl = settingValue.GetString();
    kodi::Log(ADDON_LOG_INFO, "EPG service URL changed to: %s", m_epgServiceUrl.c_str());
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
  kodi::Log(ADDON_LOG_DEBUG, "HTTP %s: %s", method.c_str(), Utils::RedactUrl(url).c_str());

  std::string apiKey, customHeaders;
  {
    std::lock_guard<std::mutex> lock(m_configMutex);
    apiKey = m_apiKey;
    customHeaders = m_customHeaders;
  }

  // 1. Basis-URL vorbereiten und den ersten Header mit '|' anhängen
  std::string formattedUrl = url + "|Content-Type=application/json";

  // 2. HTTP-Methode anhängen (falls nicht GET)
  if (method != "GET") {
    formattedUrl += "&customrequest=" + method;
  }

  // 3. Body anhängen (muss URL-encoded sein!)
  if (!body.empty()) {
    formattedUrl += "&postdata=" + Utils::UrlEncode(body);
  }

  // 4. Auth/custom headers - restored: these were dropped entirely by the
  // rapidjson migration (no Authorization header was ever sent, and
  // custom_headers was ignored), which would silently break any backend
  // that requires an API key.
  if (!apiKey.empty()) {
    formattedUrl += "&Authorization=" + Utils::UrlEncode("Bearer " + apiKey);
  }
  if (!customHeaders.empty()) {
    // The custom_headers setting predates this restore and may still be
    // entered in the pre-migration "|Header1=value1|Header2=value2" style
    // (a leading '|' with '|'-separated pairs), rather than the
    // '&'-separated style the rest of this URL now uses consistently.
    // Normalize both to the '&'-separated form Kodi expects after the
    // first '|' so either input format works.
    std::string normalized = customHeaders;
    if (!normalized.empty() && normalized.front() == '|') {
      normalized.erase(0, 1);
    }
    std::replace(normalized.begin(), normalized.end(), '|', '&');
    if (!normalized.empty()) {
      formattedUrl += "&" + normalized;
    }
  }

  kodi::vfs::CFile file;

  // 5. Datei/URL direkt öffnen (Kodi parst die Optionen automatisch heraus)
  if (!file.OpenFile(formattedUrl, ADDON_READ_NO_CACHE)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to open URL: %s", Utils::RedactUrl(url).c_str());
    return "";
  }

  std::string content;
  char buffer[16384]; // Increased from 1024 for better performance
  ssize_t bytesRead;
  while ((bytesRead = file.Read(buffer, sizeof(buffer))) > 0) {
    content.append(buffer, bytesRead);
  }
  file.Close();
  return content;
}

std::string CPVRUltimate::HttpGet(const std::string& url) {
  // Restored per-request retry (dropped in the rapidjson migration - only
  // RetryBackendCall's initial connectivity probe retried; every ordinary
  // data load, including provider/channel/recording/timer loads, was a
  // single-shot request with no retry at all).
  int maxRetries = m_maxRetries.load();
  int retryDelay = m_retryDelayMs.load();
  std::string response;
  for (int attempt = 0; attempt <= maxRetries; ++attempt) {
    response = HttpSendRequest(url, "GET", "");
    if (!response.empty()) return response;
    if (attempt < maxRetries) {
      SleepMs(retryDelay);
    }
  }
  return response;
}

bool CPVRUltimate::HttpDelete(const std::string& url) {
  std::string resp = HttpSendRequest(url, "DELETE", "");
  if (resp.empty()) {
    kodi::Log(ADDON_LOG_ERROR, "HTTP DELETE failed: %s", Utils::RedactUrl(url).c_str());
    return false;
  }
  return true;
}

bool CPVRUltimate::HttpPost(const std::string& url, const std::string& body) {
  std::string resp = HttpSendRequest(url, "POST", body);
  if (resp.empty()) {
    kodi::Log(ADDON_LOG_ERROR, "HTTP POST failed: %s", Utils::RedactUrl(url).c_str());
    return false;
  }
  return true;
}

bool CPVRUltimate::HttpPut(const std::string& url, const std::string& body) {
  std::string resp = HttpSendRequest(url, "PUT", body);
  if (resp.empty()) {
    kodi::Log(ADDON_LOG_ERROR, "HTTP PUT failed: %s", Utils::RedactUrl(url).c_str());
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

  nlohmann::json doc;
  if (Utils::ParseJsonResponse(response, doc) && doc.is_object()) {
    if (doc.contains("drm_configs_base64") && doc["drm_configs_base64"].is_string()) {
      drmConfigsBase64 = doc["drm_configs_base64"].get<std::string>();
    }
    if (doc.contains("stream_headers_base64") && doc["stream_headers_base64"].is_string()) {
      streamHeadersBase64 = doc["stream_headers_base64"].get<std::string>();
    }
    return true;
  }
  return false;
}

std::string CPVRUltimate::GetManifestUrl(const std::string& provider, const std::string& channelId) {
  return BuildApiUrl("/api/providers/" + Utils::UrlPathEncode(provider) + "/channels/" + Utils::UrlPathEncode(channelId) + "/manifest");
}

DRMConfig CPVRUltimate::GetDRMConfig(const std::string& provider, const std::string& channelId,
                                     bool isRecording) {
  DRMConfig config;
  std::string entityPath = isRecording ? "/recordings/" : "/channels/";
  std::string response = HttpGet(BuildApiUrl("/api/providers/" + Utils::UrlPathEncode(provider) + entityPath + Utils::UrlPathEncode(channelId) + "/drm"));
  if (response.empty()) return config;

  nlohmann::json document;
  if (!Utils::ParseJsonResponse(response, document)) return config;

  if (document.contains("drm_configs") && document["drm_configs"].is_object()) {
    const nlohmann::json& drmConfigs = document["drm_configs"];

    std::vector<std::pair<std::string, int>> drmSystems;
    for (auto it = drmConfigs.begin(); it != drmConfigs.end(); ++it) {
      int priority = 1;
      if (it.value().is_object() && it.value().contains("priority") && it.value()["priority"].is_number_integer()) {
        priority = it.value()["priority"].get<int>();
      }
      drmSystems.emplace_back(it.key(), priority);
    }

    if (!drmSystems.empty()) {
      auto selected = std::ranges::min_element(drmSystems,
                                               [](const auto& a, const auto& b) {
                                                 return a.second < b.second;
                                               });
      config.system = selected->first;
      const nlohmann::json& drmData = drmConfigs[selected->first];
      config.priority = selected->second;

      if (drmData.contains("license") && drmData["license"].is_object()) {
        const nlohmann::json& license = drmData["license"];
        if (license.contains("server_url") && license["server_url"].is_string())
          config.license.serverUrl = license["server_url"].get<std::string>();
        if (license.contains("req_headers") && license["req_headers"].is_string())
          config.license.reqHeaders = license["req_headers"].get<std::string>();
        if (license.contains("req_data") && license["req_data"].is_string())
          config.license.reqData = license["req_data"].get<std::string>();
        if (license.contains("server_certificate") && license["server_certificate"].is_string())
          config.license.serverCertificate = license["server_certificate"].get<std::string>();
        if (license.contains("wrapper") && license["wrapper"].is_string())
          config.license.wrapper = license["wrapper"].get<std::string>();
        if (license.contains("unwrapper") && license["unwrapper"].is_string())
          config.license.unwrapper = license["unwrapper"].get<std::string>();
      }
    }
  }
  return config;
}

nlohmann::json CPVRUltimate::GetDRMConfigJson(const std::string& provider, const std::string& channelId,
                                              bool isRecording) {
  nlohmann::json drmConfigs = nlohmann::json::object();
  std::string entityPath = isRecording ? "/recordings/" : "/channels/";
  std::string response = HttpGet(BuildApiUrl("/api/providers/" + Utils::UrlPathEncode(provider) + entityPath + Utils::UrlPathEncode(channelId) + "/drm"));
  if (response.empty()) return drmConfigs;

  nlohmann::json document;
  if (!Utils::ParseJsonResponse(response, document)) return drmConfigs;

  if (document.contains("drm_configs") && document["drm_configs"].is_object()) {
    // nlohmann::json values are regular value types - a plain assignment deep-copies
    // the sub-object, unlike rapidjson's Document/Value split which requires an
    // explicit CopyFrom(source, allocator) call to move data between documents.
    drmConfigs = document["drm_configs"];
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
      nlohmann::json drmDoc;
      if (Utils::ParseJsonResponse(decodedDrm, drmDoc) && drmDoc.is_object()) {
        if (m_useModernDrm.load()) {
          properties.emplace_back("inputstream.adaptive.drm", drmDoc.dump());
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
      nlohmann::json drmConfigs = GetDRMConfigJson(provider, channelId, isRecording);
      if (!drmConfigs.empty()) {
        properties.emplace_back("inputstream.adaptive.drm", drmConfigs.dump());
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

  EnsureInitThreadStopped();
  if (m_initThread.joinable()) {
    m_initThread.join();
  }

  m_initialized = false;
  m_initThread = std::thread(&CPVRUltimate::InitializeAsync, this);

  return PVR_ERROR_NO_ERROR;
}

// ============================================================================
// Provider Methods
// ============================================================================

PVR_ERROR CPVRUltimate::GetProvidersAmount(int& amount) {
  if (!IsReady()) { amount = 0; return PVR_ERROR_NO_ERROR; }
  amount = m_providerManager->GetProvidersAmount();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetProviders(kodi::addon::PVRProvidersResultSet& results) {
  if (!IsReady()) return PVR_ERROR_NO_ERROR;
  m_providerManager->GetProviders(results);
  return PVR_ERROR_NO_ERROR;
}

// ============================================================================
// Channel Methods
// ============================================================================

PVR_ERROR CPVRUltimate::GetChannelsAmount(int& amount) {
  if (!IsReady()) { amount = 0; return PVR_ERROR_NO_ERROR; }
  amount = m_channelManager->GetChannelsAmount();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results) {
  if (!IsReady()) return PVR_ERROR_NO_ERROR;
  m_channelManager->GetChannels(radio, results);
  return PVR_ERROR_NO_ERROR;
}

#ifdef ULTIMATE_HAS_PVR_SOURCE
PVR_ERROR CPVRUltimate::GetChannelStreamProperties(
    const kodi::addon::PVRChannel& channel,
    PVR_SOURCE source,
    std::vector<kodi::addon::PVRStreamProperty>& properties) {
#else
PVR_ERROR CPVRUltimate::GetChannelStreamProperties(
    const kodi::addon::PVRChannel& channel,
    std::vector<kodi::addon::PVRStreamProperty>& properties) {
#endif

  std::string provider, channelId;
  bool useCdm = true;
  UltimateChannel ultimateChannel;

  if (!IsReady()) {
    kodi::Log(ADDON_LOG_WARNING, "GetChannelStreamProperties called before ready");
    return PVR_ERROR_SERVER_ERROR;
  }

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

  nlohmann::json document;
  if (!Utils::ParseJsonResponse(response, document)) return PVR_ERROR_SERVER_ERROR;

  std::string manifestUrl;
  if (document.contains("manifest_url") && document["manifest_url"].is_string()) {
    manifestUrl = document["manifest_url"].get<std::string>();
  } else return PVR_ERROR_SERVER_ERROR;

  properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.adaptive");
  properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, manifestUrl);

  ApplyDRMProperties(properties, provider, channelId, useCdm, drmConfigsBase64);
  ApplyStreamHeaders(properties, streamHeadersBase64);

  return PVR_ERROR_NO_ERROR;
}

void CPVRUltimate::ApplyStreamHeaders(std::vector<kodi::addon::PVRStreamProperty>& properties,
                                      const std::string& streamHeadersBase64) {
  if (streamHeadersBase64.empty()) return;

  std::string decodedHeaders = Utils::Base64Decode(streamHeadersBase64);
  if (decodedHeaders.empty()) return;

  nlohmann::json headersDoc;
  if (!Utils::ParseJsonResponse(decodedHeaders, headersDoc) || !headersDoc.is_object()) return;

  auto buildHeaderString = [](const nlohmann::json& obj) -> std::string {
    std::string result;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
      if (!it.value().is_string()) continue;  // skip malformed entries rather than throw
      if (!result.empty()) result += "&";
      result += it.key();
      result += "=";
      result += Utils::UrlEncode(it.value().get<std::string>());
    }
    return result;
  };

  if (headersDoc.contains("manifest") && headersDoc["manifest"].is_object()) {
    std::string manifestHeaders = buildHeaderString(headersDoc["manifest"]);
    if (!manifestHeaders.empty()) {
      properties.emplace_back("inputstream.adaptive.manifest_headers", manifestHeaders);
    }
  }
  if (headersDoc.contains("segment") && headersDoc["segment"].is_object()) {
    std::string segmentHeaders = buildHeaderString(headersDoc["segment"]);
    if (!segmentHeaders.empty()) {
      properties.emplace_back("inputstream.adaptive.stream_headers", segmentHeaders);
    }
  }
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
  if (!IsReady()) return PVR_ERROR_NO_ERROR;

  auto httpGet = [this](const std::string& endpoint) -> std::string {
    return this->HttpGet(this->BuildApiUrl(endpoint));
  };
  auto parseJson = [](const std::string& response, nlohmann::json& doc) -> bool {
    return Utils::ParseJsonResponse(response, doc);
  };
  auto getChannelByUid = [this](int uid, UltimateChannel& channel) -> bool {
    return m_channelManager->GetChannelByUid(uid, channel);
  };
  // Builds a full URL against the EPG service host and performs the request directly,
  // bypassing BuildApiUrl (which always targets the backend). The endpoint passed in by
  // EPGManager already includes any versioning prefix (e.g. "/api/v1/..."), so this lambda
  // does no path-rewriting of its own - it only owns scheme+host.
  auto httpGetAbsolute = [this](const std::string& endpoint) -> std::string {
    std::string baseUrl;
    {
      std::lock_guard<std::mutex> lock(m_configMutex);
      baseUrl = m_epgServiceUrl;
    }
    if (!baseUrl.empty() && baseUrl.back() == '/') baseUrl.pop_back();
    return this->HttpGet(baseUrl + endpoint);
  };

  m_epgManager->GetEPGForChannel(channelUid, start, end, httpGet, parseJson, getChannelByUid,
                                 results, httpGetAbsolute, m_useDatabaseEpg.load());
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

  if (!IsReady()) {
    kodi::Log(ADDON_LOG_WARNING, "GetEPGTagStreamProperties called before ready");
    return PVR_ERROR_SERVER_ERROR;
  }

  // Raw HttpGet - NOT wrapped with BuildApiUrl. getManifestUrl (below) already returns a
  // fully-qualified URL (same as the live channel path), so wrapping it again here would
  // double-prefix the scheme+host (e.g. "http://host:porthttp://host:port/api/...").
  auto httpGet = [this](const std::string& url) -> std::string {
    return this->HttpGet(url);
  };
  auto parseJson = [](const std::string& response, nlohmann::json& doc) -> bool {
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
  auto getManifestUrl = [this](const std::string& provider, const std::string& channelId) -> std::string {
    return this->GetManifestUrl(provider, channelId);
  };
  auto httpGetWithHeaders = [this](const std::string& url, std::string& response,
                                    std::string& drmConfigs, std::string& headers) -> bool {
    return this->HttpGetWithHeaders(url, response, drmConfigs, headers);
  };

  std::string drmConfigsBase64, streamHeadersBase64;

  bool result = m_epgManager->GetEPGTagStreamProperties(
      tag, properties, httpGet, parseJson, getChannelInfo, getChannelByUid,
      isBackendAvailable, retryBackendCall, getManifestUrl, httpGetWithHeaders,
      m_supportsPiggyback.load(), drmConfigsBase64, streamHeadersBase64);

  if (!result) return PVR_ERROR_SERVER_ERROR;

  // Apply DRM and stream headers exactly as the live channel path does: drmConfigsBase64 /
  // streamHeadersBase64 come from the manifest response when piggyback is supported, otherwise
  // ApplyDRMProperties falls back to a separate /drm lookup via useCdm.
  int channelUid = tag.GetUniqueChannelId();
  UltimateChannel channel;
  if (m_channelManager->GetChannelByUid(channelUid, channel)) {
    ApplyDRMProperties(properties, channel.provider, channel.channelId, channel.useCdm, drmConfigsBase64);
  }
  ApplyStreamHeaders(properties, streamHeadersBase64);

  return PVR_ERROR_NO_ERROR;
}

// ============================================================================
// Recording Methods
// ============================================================================

PVR_ERROR CPVRUltimate::GetRecordingsAmount(bool deleted, int& amount) {
  if (!IsReady()) { amount = 0; return PVR_ERROR_NO_ERROR; }
  amount = m_recordingManager->GetRecordingsAmount(deleted);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results) {
  if (!IsReady()) return PVR_ERROR_NO_ERROR;
  m_recordingManager->GetRecordings(deleted, results);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::DeleteRecording(const kodi::addon::PVRRecording& recording) {
  if (!IsReady()) return PVR_ERROR_SERVER_ERROR;
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

  if (!IsReady()) {
    kodi::Log(ADDON_LOG_WARNING, "GetRecordingStreamProperties called before ready");
    return PVR_ERROR_SERVER_ERROR;
  }

  std::string recordingId = recording.GetRecordingId();

  auto buildApiUrl = [this](const std::string& endpoint) -> std::string {
    return this->BuildApiUrl(endpoint);
  };
  auto httpGet = [this](const std::string& url) -> std::string {
    return this->HttpGet(url);
  };
  auto parseJson = [](const std::string& response, nlohmann::json& doc) -> bool {
    return Utils::ParseJsonResponse(response, doc);
  };
  auto httpGetWithHeaders = [this](const std::string& url, std::string& response,
                                    std::string& drmConfigs, std::string& headers) -> bool {
    return this->HttpGetWithHeaders(url, response, drmConfigs, headers);
  };

  std::string drmConfigsBase64, streamHeadersBase64;
  bool result = m_recordingManager->GetRecordingStreamProperties(recordingId, properties,
                                                                  buildApiUrl, httpGet, parseJson,
                                                                  httpGetWithHeaders,
                                                                  m_supportsPiggyback.load(),
                                                                  drmConfigsBase64,
                                                                  streamHeadersBase64);

  if (!result) return PVR_ERROR_SERVER_ERROR;

  // Apply DRM properties. drmConfigsBase64 comes from the manifest response
  // when piggyback is supported (previously discarded here - every
  // DRM-protected recording silently fell through to a second, separate
  // /drm lookup that does not carry the catchup-scoped auth context the
  // piggybacked response does). Recordings are DRM-looked-up via the
  // /recordings/ endpoint (not /channels/) as a fallback only, since
  // rec->uniqueId is a recording id, not a channel id.
  if (auto* rec = m_recordingManager->FindRecording(recordingId)) {
    ApplyDRMProperties(properties, rec->provider, rec->uniqueId, true, drmConfigsBase64, /*isRecording=*/true);
  }
  ApplyStreamHeaders(properties, streamHeadersBase64);

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
  if (!IsReady()) return PVR_ERROR_NO_ERROR;
  m_timerManager->GetTimerTypes(types);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetTimersAmount(int& amount) {
  if (!IsReady()) { amount = 0; return PVR_ERROR_NO_ERROR; }
  amount = m_timerManager->GetTimersAmount();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetTimers(kodi::addon::PVRTimersResultSet& results) {
  if (!IsReady()) return PVR_ERROR_NO_ERROR;
  m_timerManager->GetTimers(results);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::AddTimer(const kodi::addon::PVRTimer& timer) {
  if (!IsReady()) return PVR_ERROR_SERVER_ERROR;
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
    auto parseJson = [](const std::string& response, nlohmann::json& doc) -> bool {
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
  if (!IsReady()) return PVR_ERROR_SERVER_ERROR;
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
    auto parseJson = [](const std::string& response, nlohmann::json& doc) -> bool {
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
  if (!IsReady()) return PVR_ERROR_SERVER_ERROR;
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
    auto parseJson = [](const std::string& response, nlohmann::json& doc) -> bool {
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