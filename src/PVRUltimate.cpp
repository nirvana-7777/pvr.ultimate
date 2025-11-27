#include "PVRUltimate.h"
#include <kodi/General.h>
#include <sstream>
#include <algorithm>
#include <thread>
#include <chrono>

CPVRUltimate::CPVRUltimate() : m_nextChannelNumber(1), m_backendAvailable(false), m_retryCount(0) {
  kodi::Log(ADDON_LOG_INFO, "Ultimate PVR Client starting...");

  // Load backend settings
  m_backendUrl = kodi::addon::GetSettingString("backend_url", "localhost");
  m_backendPort = kodi::addon::GetSettingInt("backend_port", 7777);

  kodi::Log(ADDON_LOG_DEBUG, "Backend URL: %s:%d", m_backendUrl.c_str(), m_backendPort);

  // Retry backend connection
  if (RetryBackendCall("initialization")) {
    if (!LoadProviders()) {
      kodi::Log(ADDON_LOG_ERROR, "Failed to load providers");
    }

    if (!LoadChannels()) {
      kodi::Log(ADDON_LOG_ERROR, "Failed to load channels");
    }
  }

  kodi::Log(ADDON_LOG_INFO, "Ultimate PVR Client loaded %d channels from %d providers",
            static_cast<int>(m_channels.size()), static_cast<int>(m_providers.size()));
}

CPVRUltimate::~CPVRUltimate() {
  kodi::Log(ADDON_LOG_INFO, "Ultimate PVR Client stopping...");
}

bool CPVRUltimate::RetryBackendCall(const std::string& operationName) {
  for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
    std::string testUrl = BuildApiUrl("/api/providers");
    std::string response = HttpGet(testUrl);

    if (!response.empty()) {
      kodi::Log(ADDON_LOG_INFO, "Backend connection established on attempt %d", attempt);
      m_backendAvailable = true;
      return true;
    }

    if (attempt < MAX_RETRIES) {
      int delay = RETRY_DELAY_MS * attempt; // Exponential backoff
      kodi::Log(ADDON_LOG_WARNING, "Backend not ready for %s, attempt %d/%d, retrying in %dms...",
                operationName.c_str(), attempt, MAX_RETRIES, delay);
      SleepMs(delay);
    }
  }

  kodi::Log(ADDON_LOG_ERROR, "Backend unavailable for %s after %d attempts",
            operationName.c_str(), MAX_RETRIES);
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
    m_backendUrl = settingValue.GetString();
    kodi::Log(ADDON_LOG_INFO, "Backend URL changed to: %s", m_backendUrl.c_str());
    return ADDON_STATUS_NEED_RESTART;
  } else if (settingName == "backend_port") {
    m_backendPort = settingValue.GetInt();
    kodi::Log(ADDON_LOG_INFO, "Backend port changed to: %d", m_backendPort);
    return ADDON_STATUS_NEED_RESTART;
  }

  return ADDON_STATUS_OK;
}

std::string CPVRUltimate::BuildApiUrl(const std::string& endpoint) {
  std::ostringstream url;
  url << "http://" << m_backendUrl << ":" << m_backendPort << endpoint;
  return url.str();
}

std::string CPVRUltimate::HttpGet(const std::string& url) {
  kodi::Log(ADDON_LOG_DEBUG, "HTTP GET: %s", url.c_str());

  kodi::vfs::CFile file;
  if (!file.OpenFile(url)) {
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

  if (content.empty()) {
    kodi::Log(ADDON_LOG_DEBUG, "Empty response from: %s", url.c_str());
  }

  return content;
}

bool CPVRUltimate::ParseJsonResponse(const std::string& response, Json::Value& root) {
  Json::CharReaderBuilder builder;
  std::string errs;
  std::istringstream stream(response);

  if (!Json::parseFromStream(builder, stream, &root, &errs)) {
    kodi::Log(ADDON_LOG_ERROR, "JSON parse error: %s", errs.c_str());
    return false;
  }

  return true;
}

bool CPVRUltimate::IsProviderEnabled(const std::string& provider) {
  // All providers from backend are enabled by default
  return true;
}

int CPVRUltimate::GenerateProviderUniqueId(const std::string& providerName)
{
  std::string concat(providerName);

  const char* calcString = concat.c_str();
  int uniqueId = 0;
  int c;
  while ((c = *calcString++))
    uniqueId = ((uniqueId << 5) + uniqueId) + c; /* iId * 33 + c */

  return abs(uniqueId);
}

bool CPVRUltimate::LoadProviders() {
  std::string url = BuildApiUrl("/api/providers");
  std::string response = HttpGet(url);

  if (response.empty()) {
    kodi::Log(ADDON_LOG_ERROR, "Empty response from providers endpoint");
    return false;
  }

  Json::Value root;
  if (!ParseJsonResponse(response, root)) {
    return false;
  }

  if (!root.isMember("providers") || !root["providers"].isArray()) {
    kodi::Log(ADDON_LOG_ERROR, "Invalid providers response format");
    return false;
  }

  m_providers.clear();
  m_providerIdMap.clear(); // Clear the lookup map

  const Json::Value& providers = root["providers"];

  for (const auto& provider : providers) {
    if (provider.isObject() && provider.isMember("name")) {
      UltimateProvider p;

      // Extract provider data from the new object format
      p.name = provider.get("name", "").asString();

      // Use label if available, otherwise fall back to name
      if (provider.isMember("label") && !provider["label"].asString().empty()) {
        p.label = provider["label"].asString();
      } else {
        p.label = p.name;
      }

      // Store country information if available
      if (provider.isMember("country")) {
        p.country = provider["country"].asString();
      }

      if (provider.isMember("logo")) {
        p.logo = provider["logo"].asString();
      }

      p.enabled = IsProviderEnabled(p.name);
      p.uniqueId = GenerateProviderUniqueId(p.name); // Generate once and store

      m_providers.push_back(p);
      m_providerIdMap[p.name] = p.uniqueId; // Store in lookup map

      kodi::Log(ADDON_LOG_DEBUG, "Found provider: %s (label: %s, country: %s, logo: %s, enabled: %d, UID: %d)",
                p.name.c_str(), p.label.c_str(), p.country.c_str(), p.logo.c_str(), p.enabled, p.uniqueId);
    } else if (provider.isString()) {
      // Fallback: handle old string format for backward compatibility
      UltimateProvider p;
      p.name = provider.asString();
      p.label = p.name; // Use name as label
      p.enabled = IsProviderEnabled(p.name);
      p.uniqueId = GenerateProviderUniqueId(p.name);

      m_providers.push_back(p);
      m_providerIdMap[p.name] = p.uniqueId;

      kodi::Log(ADDON_LOG_DEBUG, "Found provider (legacy format): %s (UID: %d)",
                p.name.c_str(), p.uniqueId);
    }
  }

  // Log default country if available
  if (root.isMember("default_country")) {
    kodi::Log(ADDON_LOG_DEBUG, "Default country from backend: %s",
              root["default_country"].asString().c_str());
  }

  kodi::Log(ADDON_LOG_INFO, "Loaded %d providers", static_cast<int>(m_providers.size()));
  return true;
}

bool CPVRUltimate::LoadChannels() {
  m_channels.clear();
  m_nextChannelNumber = 1; // Reset for fallback scenarios

  for (const auto& provider : m_providers) {
    if (provider.enabled) {
      if (!LoadChannelsForProvider(provider.name)) {
        kodi::Log(ADDON_LOG_WARNING, "Failed to load channels for provider: %s", provider.name.c_str());
      }
    }
  }

  // Log final channel numbering summary
  kodi::Log(ADDON_LOG_INFO, "Final channel load: %d channels with provider offset %s",
            static_cast<int>(m_channels.size()),
            (m_providers.size() > 1) ? "applied" : "not applied");

  return !m_channels.empty();
}

bool CPVRUltimate::LoadChannelsForProvider(const std::string& provider) {
  std::string url = BuildApiUrl("/api/providers/" + provider + "/channels");
  std::string response = HttpGet(url);

  if (response.empty()) {
    kodi::Log(ADDON_LOG_ERROR, "Empty response from channels endpoint for %s", provider.c_str());
    return false;
  }

  Json::Value root;
  if (!ParseJsonResponse(response, root)) {
    return false;
  }

  if (!root.isMember("channels") || !root["channels"].isArray()) {
    kodi::Log(ADDON_LOG_ERROR, "Invalid channels response format for %s", provider.c_str());
    return false;
  }

  const Json::Value& channels = root["channels"];

  // Determine if we need to add offset (if we have multiple providers)
  int providerOffset = (m_providers.size() > 1) ? 1000 : 0;
  kodi::Log(ADDON_LOG_DEBUG, "Provider offset for %s: %d", provider.c_str(), providerOffset);

  for (const auto& channelJson : channels) {
    UltimateChannel channel;

    // Parse channel data
    channel.channelName = channelJson.get("Name", "").asString();
    channel.channelId = channelJson.get("Id", "").asString();
    channel.provider = provider; // Always use the provider we're loading from
    channel.iconPath = channelJson.get("LogoUrl", "").asString();

    // Use backend-provided channel number with provider offset
    if (channelJson.isMember("ChannelNumber") && channelJson["ChannelNumber"].isInt()) {
      int backendChannelNumber = channelJson["ChannelNumber"].asInt();
      channel.channelNumber = backendChannelNumber + providerOffset;

      // Update next channel number tracker for fallback scenarios
      if (channel.channelNumber >= m_nextChannelNumber) {
        m_nextChannelNumber = channel.channelNumber + 1;
      }
    } else {
      // Fallback to auto-increment if no channel number from backend
      channel.channelNumber = m_nextChannelNumber++;
    }

    // Generate unique ID
    channel.uniqueId = provider + ":" + channel.channelId;

    // Stream properties
    channel.mode = channelJson.get("Mode", "live").asString();
    channel.sessionManifest = channelJson.get("SessionManifest", false).asBool();
    channel.manifest = channelJson.get("Manifest", "").asString();
    channel.manifestScript = channelJson.get("ManifestScript", "").asString();
    channel.useCdm = channelJson.get("UseCdm", true).asBool();
    channel.cdmMode = channelJson.get("CdmMode", "external").asString();
    channel.contentType = channelJson.get("ContentType", "LIVE").asString();
    channel.country = channelJson.get("Country", "").asString();
    channel.language = channelJson.get("Language", "de").asString();
    channel.streamingFormat = channelJson.get("StreamingFormat", "").asString();

    // Assume TV channels (not radio)
    channel.isRadio = false;

    m_channels.push_back(channel);

    kodi::Log(ADDON_LOG_DEBUG, "Loaded channel: %s (Backend#: %d, Kodi#: %d, Provider: %s)",
              channel.channelName.c_str(),
              channelJson.get("ChannelNumber", 0).asInt(),
              channel.channelNumber,
              channel.provider.c_str());
  }

  kodi::Log(ADDON_LOG_INFO, "Loaded %d channels from provider %s (offset: %d)",
            static_cast<int>(channels.size()), provider.c_str(), providerOffset);

  return true;
}

std::string CPVRUltimate::GetManifestUrl(const std::string& provider,
                                          const std::string& channelId) {
  std::string url = BuildApiUrl("/api/providers/" + provider + "/channels/" +
                                channelId + "/manifest");
  return url;
}

DRMConfig CPVRUltimate::GetDRMConfig(const std::string& provider,
                                      const std::string& channelId) {
  DRMConfig config;

  std::string url = BuildApiUrl("/api/providers/" + provider + "/channels/" +
                                channelId + "/drm");
  std::string response = HttpGet(url);

  if (response.empty()) {
    kodi::Log(ADDON_LOG_ERROR, "Empty response from DRM endpoint");
    return config;
  }

  Json::Value root;
  if (!ParseJsonResponse(response, root)) {
    return config;
  }

  if (!root.isMember("drm_configs") || !root["drm_configs"].isArray()) {
    kodi::Log(ADDON_LOG_ERROR, "Invalid DRM config response format");
    return config;
  }

  const Json::Value& drmConfigs = root["drm_configs"];

  if (drmConfigs.size() > 0) {
    const Json::Value& firstConfig = drmConfigs[0];

    for (const auto& key : firstConfig.getMemberNames()) {
      config.system = key;
      const Json::Value& drmData = firstConfig[key];

      config.priority = drmData.get("priority", 1).asInt();

      if (drmData.isMember("license")) {
        const Json::Value& license = drmData["license"];

        config.license.serverUrl = license.get("server_url", "").asString();
        config.license.serverCertificate = license.get("server_certificate", "").asString();
        config.license.reqHeaders = license.get("req_headers", "").asString();
        config.license.reqData = license.get("req_data", "").asString();
        config.license.reqParams = license.get("req_params", "").asString();
        config.license.useHttpGetRequest = license.get("use_http_get_request", false).asBool();
        config.license.wrapper = license.get("wrapper", "").asString();
        config.license.unwrapper = license.get("unwrapper", "").asString();
      }
      break;
    }
  }

  kodi::Log(ADDON_LOG_DEBUG, "Got DRM config: system=%s, license_url=%s",
            config.system.c_str(), config.license.serverUrl.c_str());

  return config;
}

// PVR Capability Methods
PVR_ERROR CPVRUltimate::GetCapabilities(kodi::addon::PVRCapabilities& capabilities) {
  capabilities.SetSupportsEPG(false);
  capabilities.SetSupportsTV(true);
  capabilities.SetSupportsRadio(false);
  capabilities.SetSupportsRecordings(false);
  capabilities.SetSupportsTimers(false);
  capabilities.SetSupportsChannelGroups(true);
  capabilities.SetSupportsChannelScan(false);
  capabilities.SetHandlesInputStream(true);
  capabilities.SetHandlesDemuxing(false);

  // ENABLE PROVIDER SUPPORT
  capabilities.SetSupportsProviders(true);

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
  connection = m_backendUrl + ":" + std::to_string(m_backendPort);
  return PVR_ERROR_NO_ERROR;
}

// Provider Methods
PVR_ERROR CPVRUltimate::GetProvidersAmount(int& amount) {
  amount = 0;
  for (const auto& provider : m_providers) {
    if (provider.enabled) {
      amount++;
    }
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetProviders(kodi::addon::PVRProvidersResultSet& results) {
  for (const auto& provider : m_providers) {
    if (provider.enabled) {
      kodi::addon::PVRProvider kodiProvider;

      // Use label for display name if available, otherwise fall back to name
      if (!provider.label.empty() && provider.label != provider.name) {
        kodiProvider.SetName(provider.label);
      } else {
        kodiProvider.SetName(provider.name);
      }

      kodiProvider.SetType(PVR_PROVIDER_TYPE_IPTV);
      kodiProvider.SetIconPath(provider.logo);
      kodiProvider.SetUniqueId(provider.uniqueId); // Use stored ID

      if (!provider.country.empty()) {
        kodiProvider.SetCountries({provider.country});
      }

      results.Add(kodiProvider);

      kodi::Log(ADDON_LOG_DEBUG, "Added provider to results: %s (label: %s, UID: %d)",
                provider.name.c_str(), provider.label.c_str(), provider.uniqueId);
    }
  }
  return PVR_ERROR_NO_ERROR;
}

// Channel Methods
PVR_ERROR CPVRUltimate::GetChannelsAmount(int& amount) {
  amount = static_cast<int>(m_channels.size());
  return PVR_ERROR_NO_ERROR;
}

// Channel Methods
PVR_ERROR CPVRUltimate::GetChannels(bool radio,
                                     kodi::addon::PVRChannelsResultSet& results) {
  for (const auto& channel : m_channels) {
    if (channel.isRadio == radio) {
      kodi::addon::PVRChannel kodiChannel;

      kodiChannel.SetUniqueId(channel.channelNumber);
      kodiChannel.SetIsRadio(channel.isRadio);
      kodiChannel.SetChannelNumber(channel.channelNumber);
      kodiChannel.SetChannelName(channel.channelName);
      kodiChannel.SetIconPath(channel.iconPath);
      kodiChannel.SetIsHidden(false);
      kodiChannel.SetHasArchive(false);

      // Efficient lookup - no regeneration
      auto it = m_providerIdMap.find(channel.provider);
      if (it != m_providerIdMap.end()) {
        kodiChannel.SetClientProviderUid(it->second);
      } else {
        kodi::Log(ADDON_LOG_WARNING, "Provider ID not found for: %s", channel.provider.c_str());
      }

      results.Add(kodiChannel);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetChannelStreamProperties(
    const kodi::addon::PVRChannel& channel,
    PVR_SOURCE source,
    std::vector<kodi::addon::PVRStreamProperty>& properties) {

  // Find our channel
  UltimateChannel* ultimateChannel = nullptr;
  for (auto& ch : m_channels) {
    if (ch.channelNumber == channel.GetUniqueId()) {
      ultimateChannel = &ch;
      break;
    }
  }

  if (!ultimateChannel) {
    kodi::Log(ADDON_LOG_ERROR, "Channel not found: %d", channel.GetUniqueId());
    return PVR_ERROR_SERVER_ERROR;
  }

  kodi::Log(ADDON_LOG_INFO, "Getting stream properties for: %s (Provider: %s)",
            ultimateChannel->channelName.c_str(), ultimateChannel->provider.c_str());

  // Retry if backend became unavailable
  if (!m_backendAvailable && !RetryBackendCall("stream playback")) {
    kodi::Log(ADDON_LOG_ERROR, "Backend unavailable for stream playback");
    return PVR_ERROR_SERVER_ERROR;
  }

  // Get manifest URL from backend API
  std::string manifestApiUrl = GetManifestUrl(ultimateChannel->provider,
                                              ultimateChannel->channelId);

  kodi::Log(ADDON_LOG_DEBUG, "Calling manifest API: %s", manifestApiUrl.c_str());

  std::string response = HttpGet(manifestApiUrl);
  if (response.empty()) {
    kodi::Log(ADDON_LOG_ERROR, "Empty response from manifest API");
    return PVR_ERROR_SERVER_ERROR;
  }

  // Parse JSON response to extract manifest_url
  Json::Value root;
  if (!ParseJsonResponse(response, root)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to parse manifest API response");
    return PVR_ERROR_SERVER_ERROR;
  }

  // Check if we have an error response
  if (root.isMember("error")) {
    kodi::Log(ADDON_LOG_ERROR, "Manifest API returned error: %s", root["error"].asString().c_str());
    return PVR_ERROR_SERVER_ERROR;
  }

  // Extract the actual manifest URL from JSON
  std::string manifestUrl;
  if (root.isMember("manifest_url")) {
    manifestUrl = root["manifest_url"].asString();
    kodi::Log(ADDON_LOG_INFO, "Extracted manifest URL: %s", manifestUrl.c_str());
  } else {
    kodi::Log(ADDON_LOG_ERROR, "No manifest_url in API response");
    return PVR_ERROR_SERVER_ERROR;
  }

  // Log additional info from response
  kodi::Log(ADDON_LOG_DEBUG, "Manifest API response - provider: %s, channel_id: %s",
            root.get("provider", "").asString().c_str(),
            root.get("channel_id", "").asString().c_str());

  // Set up inputstream.adaptive - no need to set manifest_type (auto-detected)
  properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.adaptive");
  properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, manifestUrl);

  // Get DRM configuration if needed and use new drm_legacy property
  if (ultimateChannel->useCdm) {
    DRMConfig drmConfig = GetDRMConfig(ultimateChannel->provider,
                                        ultimateChannel->channelId);

    if (!drmConfig.system.empty() && !drmConfig.license.serverUrl.empty()) {
      // Build drm_legacy property in the new format: [DRM KeySystem]|[License URL]|[Headers]
      std::string drmLegacyValue = drmConfig.system + "|" + drmConfig.license.serverUrl;

      // Add license headers if available
      if (!drmConfig.license.reqHeaders.empty()) {
        drmLegacyValue += "|" + drmConfig.license.reqHeaders;
      }

      properties.emplace_back("inputstream.adaptive.drm_legacy", drmLegacyValue);

      kodi::Log(ADDON_LOG_DEBUG, "Set DRM legacy config: %s", drmLegacyValue.c_str());

      // Note: We're not using the old license_type and license_key properties anymore
    } else if (!drmConfig.system.empty()) {
      kodi::Log(ADDON_LOG_DEBUG, "DRM system %s configured but no license URL provided",
                drmConfig.system.c_str());

      // Some DRM systems might not need a license URL (like ClearKey with key IDs in manifest)
      if (drmConfig.system == "org.w3.clearkey") {
        properties.emplace_back("inputstream.adaptive.drm_legacy", drmConfig.system);
        kodi::Log(ADDON_LOG_DEBUG, "Set ClearKey DRM without license URL");
      }
    }
  }

  return PVR_ERROR_NO_ERROR;
}

// Channel Group Methods - Keep minimal for backward compatibility
PVR_ERROR CPVRUltimate::GetChannelGroupsAmount(int& amount) {
  amount = 1; // Just "All Channels" group
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetChannelGroups(bool radio,
                                          kodi::addon::PVRChannelGroupsResultSet& results) {
  if (radio) {
    return PVR_ERROR_NO_ERROR;
  }

  // Just provide "All Channels" group
  // Providers are now handled through native provider support
  kodi::addon::PVRChannelGroup allGroup;
  allGroup.SetIsRadio(false);
  allGroup.SetGroupName("All Channels");
  results.Add(allGroup);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetChannelGroupMembers(
    const kodi::addon::PVRChannelGroup& group,
    kodi::addon::PVRChannelGroupMembersResultSet& results) {

  std::string groupName = group.GetGroupName();

  // Only handle "All Channels" group now
  if (groupName == "All Channels") {
    for (const auto& channel : m_channels) {
      kodi::addon::PVRChannelGroupMember member;
      member.SetGroupName(groupName);
      member.SetChannelUniqueId(channel.channelNumber);
      member.SetChannelNumber(channel.channelNumber);

      results.Add(member);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetSignalStatus(int channelUid,
                                         kodi::addon::PVRSignalStatus& signalStatus) {
  signalStatus.SetAdapterName("Ultimate PVR");

  // Set connection status based on backend availability
  if (m_backendAvailable) {
    signalStatus.SetAdapterStatus("Connected");
  } else {
    signalStatus.SetAdapterStatus("Disconnected");
  }

  return PVR_ERROR_NO_ERROR;
}

ADDONCREATOR(CPVRUltimate)