#include "PVRUltimate.h"
#include <kodi/General.h>
#include <sstream>
#include <algorithm>
#include <thread>
#include <chrono>

CPVRUltimate::CPVRUltimate() : m_nextChannelNumber(1), m_backendAvailable(false), m_retryCount(0) {
  kodi::Log(ADDON_LOG_INFO, "Ultimate PVR Client starting...");

  // Detect if we should use modern DRM format (inputstream.adaptive v22+)
  DetectInputstreamVersion();

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

void CPVRUltimate::DetectInputstreamVersion() {
  m_useModernDrm = false;

  // Get Kodi version - inputstream.adaptive version matches Kodi version
  kodi_version_t kodiVersion;
  kodi::KodiVersion(kodiVersion);

  // inputstream.adaptive v22+ uses the new DRM format
  m_useModernDrm = (kodiVersion.major >= 22);

  kodi::Log(ADDON_LOG_INFO, "Kodi version: %d.%d.%s, modern DRM (v22+): %s",
            kodiVersion.major, kodiVersion.minor,
            kodiVersion.revision.c_str(),
            m_useModernDrm ? "yes" : "no");
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

bool CPVRUltimate::ParseJsonResponse(const std::string& response, rapidjson::Document& document) {
  rapidjson::ParseResult result = document.Parse(response.c_str());

  if (result.IsError()) {
    kodi::Log(ADDON_LOG_ERROR, "JSON parse error: %s (Offset: %zu)",
              rapidjson::GetParseError_En(result.Code()), result.Offset());
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

  rapidjson::Document document;
  if (!ParseJsonResponse(response, document)) {
    return false;
  }

  if (!document.HasMember("providers") || !document["providers"].IsArray()) {
    kodi::Log(ADDON_LOG_ERROR, "Invalid providers response format");
    return false;
  }

  m_providers.clear();
  m_providerIdMap.clear();

  const rapidjson::Value& providers = document["providers"];

  for (const auto& provider : providers.GetArray()) {
    if (provider.IsObject() && provider.HasMember("name")) {
      UltimateProvider p;

      // Extract provider data
      p.name = provider["name"].GetString();

      // Use label if available, otherwise fall back to name
      if (provider.HasMember("label") && provider["label"].IsString() &&
          strlen(provider["label"].GetString()) > 0) {
        p.label = provider["label"].GetString();
      } else {
        p.label = p.name;
      }

      // Store country information if available
      if (provider.HasMember("country") && provider["country"].IsString()) {
        p.country = provider["country"].GetString();
      }

      if (provider.HasMember("logo") && provider["logo"].IsString()) {
        p.logo = provider["logo"].GetString();
      }

      p.enabled = IsProviderEnabled(p.name);
      p.uniqueId = GenerateProviderUniqueId(p.name);

      m_providers.push_back(p);
      m_providerIdMap[p.name] = p.uniqueId;

      kodi::Log(ADDON_LOG_DEBUG, "Found provider: %s (label: %s, country: %s, logo: %s, enabled: %d, UID: %d)",
                p.name.c_str(), p.label.c_str(), p.country.c_str(), p.logo.c_str(), p.enabled, p.uniqueId);
    } else if (provider.IsString()) {
      // Fallback: handle old string format
      UltimateProvider p;
      p.name = provider.GetString();
      p.label = p.name;
      p.enabled = IsProviderEnabled(p.name);
      p.uniqueId = GenerateProviderUniqueId(p.name);

      m_providers.push_back(p);
      m_providerIdMap[p.name] = p.uniqueId;

      kodi::Log(ADDON_LOG_DEBUG, "Found provider (legacy format): %s (UID: %d)",
                p.name.c_str(), p.uniqueId);
    }
  }

  // Log default country if available
  if (document.HasMember("default_country") && document["default_country"].IsString()) {
    kodi::Log(ADDON_LOG_DEBUG, "Default country from backend: %s",
              document["default_country"].GetString());
  }

  kodi::Log(ADDON_LOG_INFO, "Loaded %d providers", static_cast<int>(m_providers.size()));
  return true;
}

bool CPVRUltimate::LoadChannels() {
  m_channels.clear();
  m_nextChannelNumber = 1;

  for (const auto& provider : m_providers) {
    if (provider.enabled) {
      if (!LoadChannelsForProvider(provider.name)) {
        kodi::Log(ADDON_LOG_WARNING, "Failed to load channels for provider: %s", provider.name.c_str());
      }
    }
  }

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

  rapidjson::Document document;
  if (!ParseJsonResponse(response, document)) {
    return false;
  }

  if (!document.HasMember("channels") || !document["channels"].IsArray()) {
    kodi::Log(ADDON_LOG_ERROR, "Invalid channels response format for %s", provider.c_str());
    return false;
  }

  const rapidjson::Value& channels = document["channels"];

  // Determine if we need to add offset (if we have multiple providers)
  int providerOffset = (m_providers.size() > 1) ? 1000 : 0;
  kodi::Log(ADDON_LOG_DEBUG, "Provider offset for %s: %d", provider.c_str(), providerOffset);

  for (const auto& channelJson : channels.GetArray()) {
    UltimateChannel channel;

    // Parse channel data
    if (channelJson.HasMember("Name") && channelJson["Name"].IsString()) {
      channel.channelName = channelJson["Name"].GetString();
    }

    if (channelJson.HasMember("Id") && channelJson["Id"].IsString()) {
      channel.channelId = channelJson["Id"].GetString();
    }

    channel.provider = provider;

    if (channelJson.HasMember("LogoUrl") && channelJson["LogoUrl"].IsString()) {
      channel.iconPath = channelJson["LogoUrl"].GetString();
    }

    // Use backend-provided channel number with provider offset
    if (channelJson.HasMember("ChannelNumber") && channelJson["ChannelNumber"].IsInt()) {
      int backendChannelNumber = channelJson["ChannelNumber"].GetInt();
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
    if (channelJson.HasMember("Mode") && channelJson["Mode"].IsString()) {
      channel.mode = channelJson["Mode"].GetString();
    } else {
      channel.mode = "live";
    }

    if (channelJson.HasMember("SessionManifest") && channelJson["SessionManifest"].IsBool()) {
      channel.sessionManifest = channelJson["SessionManifest"].GetBool();
    } else {
      channel.sessionManifest = false;
    }

    if (channelJson.HasMember("Manifest") && channelJson["Manifest"].IsString()) {
      channel.manifest = channelJson["Manifest"].GetString();
    }

    if (channelJson.HasMember("ManifestScript") && channelJson["ManifestScript"].IsString()) {
      channel.manifestScript = channelJson["ManifestScript"].GetString();
    }

    if (channelJson.HasMember("UseCdm") && channelJson["UseCdm"].IsBool()) {
      channel.useCdm = channelJson["UseCdm"].GetBool();
    } else {
      channel.useCdm = true;
    }

    if (channelJson.HasMember("CdmMode") && channelJson["CdmMode"].IsString()) {
      channel.cdmMode = channelJson["CdmMode"].GetString();
    } else {
      channel.cdmMode = "external";
    }

    if (channelJson.HasMember("ContentType") && channelJson["ContentType"].IsString()) {
      channel.contentType = channelJson["ContentType"].GetString();
    } else {
      channel.contentType = "LIVE";
    }

    if (channelJson.HasMember("Country") && channelJson["Country"].IsString()) {
      channel.country = channelJson["Country"].GetString();
    }

    if (channelJson.HasMember("Language") && channelJson["Language"].IsString()) {
      channel.language = channelJson["Language"].GetString();
    } else {
      channel.language = "de";
    }

    if (channelJson.HasMember("StreamingFormat") && channelJson["StreamingFormat"].IsString()) {
      channel.streamingFormat = channelJson["StreamingFormat"].GetString();
    }

    // Assume TV channels (not radio)
    channel.isRadio = false;

    m_channels.push_back(channel);

    kodi::Log(ADDON_LOG_DEBUG, "Loaded channel: %s (Backend#: %d, Kodi#: %d, Provider: %s)",
              channel.channelName.c_str(),
              channelJson.HasMember("ChannelNumber") && channelJson["ChannelNumber"].IsInt() ?
                channelJson["ChannelNumber"].GetInt() : 0,
              channel.channelNumber,
              channel.provider.c_str());
  }

  kodi::Log(ADDON_LOG_INFO, "Loaded %d channels from provider %s (offset: %d)",
            static_cast<int>(channels.Size()), provider.c_str(), providerOffset);

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

  rapidjson::Document document;
  if (!ParseJsonResponse(response, document)) {
    return config;
  }

  // Handle new object format from backend
  if (document.HasMember("drm_configs") && document["drm_configs"].IsObject()) {
    const rapidjson::Value& drmConfigs = document["drm_configs"];

    // Get first DRM system from the object
    for (auto it = drmConfigs.MemberBegin(); it != drmConfigs.MemberEnd(); ++it) {
      const std::string firstKey = it->name.GetString();
      config.system = firstKey;

      const rapidjson::Value& drmData = it->value;

      if (drmData.HasMember("priority") && drmData["priority"].IsInt()) {
        config.priority = drmData["priority"].GetInt();
      } else {
        config.priority = 1;
      }

      if (drmData.HasMember("license") && drmData["license"].IsObject()) {
        const rapidjson::Value& license = drmData["license"];

        if (license.HasMember("server_url") && license["server_url"].IsString()) {
          config.license.serverUrl = license["server_url"].GetString();
        }

        if (license.HasMember("server_certificate") && license["server_certificate"].IsString()) {
          config.license.serverCertificate = license["server_certificate"].GetString();
        }

        if (license.HasMember("req_headers") && license["req_headers"].IsString()) {
          config.license.reqHeaders = license["req_headers"].GetString();
        }

        if (license.HasMember("req_data") && license["req_data"].IsString()) {
          config.license.reqData = license["req_data"].GetString();
        }

        if (license.HasMember("req_params") && license["req_params"].IsString()) {
          config.license.reqParams = license["req_params"].GetString();
        }

        if (license.HasMember("use_http_get_request") && license["use_http_get_request"].IsBool()) {
          config.license.useHttpGetRequest = license["use_http_get_request"].GetBool();
        } else {
          config.license.useHttpGetRequest = false;
        }

        if (license.HasMember("wrapper") && license["wrapper"].IsString()) {
          config.license.wrapper = license["wrapper"].GetString();
        }

        if (license.HasMember("unwrapper") && license["unwrapper"].IsString()) {
          config.license.unwrapper = license["unwrapper"].GetString();
        }
      }

      break; // Only process the first DRM system
    }
  } else {
    kodi::Log(ADDON_LOG_ERROR, "Invalid DRM config response format - expected object");
    return config;
  }

  kodi::Log(ADDON_LOG_DEBUG, "Got DRM config: system=%s, license_url=%s",
            config.system.c_str(), config.license.serverUrl.c_str());

  return config;
}

rapidjson::Document CPVRUltimate::GetDRMConfigJson(const std::string& provider,
                                                   const std::string& channelId) {
  rapidjson::Document drmConfigs(rapidjson::kObjectType);

  std::string url = BuildApiUrl("/api/providers/" + provider + "/channels/" +
                                channelId + "/drm");
  std::string response = HttpGet(url);

  if (response.empty()) {
    kodi::Log(ADDON_LOG_DEBUG, "Empty response from DRM endpoint for %s/%s",
              provider.c_str(), channelId.c_str());
    return drmConfigs;
  }

  rapidjson::Document document;
  if (!ParseJsonResponse(response, document)) {
    return drmConfigs;
  }

  if (document.HasMember("drm_configs") && document["drm_configs"].IsObject()) {
    drmConfigs.CopyFrom(document["drm_configs"], drmConfigs.GetAllocator());
    kodi::Log(ADDON_LOG_DEBUG, "Got DRM config object for %s/%s",
              provider.c_str(), channelId.c_str());
  } else {
    kodi::Log(ADDON_LOG_DEBUG, "No drm_configs object found in response for %s/%s",
              provider.c_str(), channelId.c_str());
  }

  return drmConfigs;
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
      kodiProvider.SetUniqueId(provider.uniqueId);

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

      // Efficient lookup
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
  rapidjson::Document document;
  if (!ParseJsonResponse(response, document)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to parse manifest API response");
    return PVR_ERROR_SERVER_ERROR;
  }

  // Check if we have an error response
  if (document.HasMember("error") && document["error"].IsString()) {
    kodi::Log(ADDON_LOG_ERROR, "Manifest API returned error: %s",
              document["error"].GetString());
    return PVR_ERROR_SERVER_ERROR;
  }

  // Extract the actual manifest URL from JSON
  std::string manifestUrl;
  if (document.HasMember("manifest_url") && document["manifest_url"].IsString()) {
    manifestUrl = document["manifest_url"].GetString();
    kodi::Log(ADDON_LOG_INFO, "Extracted manifest URL: %s", manifestUrl.c_str());
  } else {
    kodi::Log(ADDON_LOG_ERROR, "No manifest_url in API response");
    return PVR_ERROR_SERVER_ERROR;
  }

  // Log additional info from response
  if (document.HasMember("provider") && document["provider"].IsString() &&
      document.HasMember("channel_id") && document["channel_id"].IsString()) {
    kodi::Log(ADDON_LOG_DEBUG, "Manifest API response - provider: %s, channel_id: %s",
              document["provider"].GetString(),
              document["channel_id"].GetString());
  }

  // Set up inputstream.adaptive
  properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.adaptive");
  properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, manifestUrl);

  // Get DRM configuration if needed
  if (ultimateChannel->useCdm) {
    if (m_useModernDrm) {
      // Version 22+: Use new JSON-based DRM config format
      rapidjson::Document drmConfigs = GetDRMConfigJson(ultimateChannel->provider,
                                                        ultimateChannel->channelId);

      if (!drmConfigs.ObjectEmpty()) {
        // Convert JSON to string
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        drmConfigs.Accept(writer);

        std::string drmConfigStr = buffer.GetString();
        properties.emplace_back("inputstream.adaptive.drm", drmConfigStr);

        kodi::Log(ADDON_LOG_DEBUG, "Set modern DRM config (%zu bytes) for %s/%s",
                  drmConfigStr.size(), ultimateChannel->provider.c_str(),
                  ultimateChannel->channelId.c_str());
      } else {
        kodi::Log(ADDON_LOG_DEBUG, "No DRM configs returned for %s/%s",
                  ultimateChannel->provider.c_str(), ultimateChannel->channelId.c_str());
      }
    } else {
      // Legacy version (<22): Use old drm_legacy format
      DRMConfig drmConfig = GetDRMConfig(ultimateChannel->provider,
                                         ultimateChannel->channelId);

      if (!drmConfig.system.empty() && !drmConfig.license.serverUrl.empty()) {
        // Build drm_legacy property in the format: [DRM KeySystem]|[License URL]|[Headers]
        std::string drmLegacyValue = drmConfig.system + "|" + drmConfig.license.serverUrl;

        // Add license headers if available
        if (!drmConfig.license.reqHeaders.empty()) {
          drmLegacyValue += "|" + drmConfig.license.reqHeaders;
        }

        properties.emplace_back("inputstream.adaptive.drm_legacy", drmLegacyValue);

        kodi::Log(ADDON_LOG_DEBUG, "Set legacy DRM config: %s", drmLegacyValue.c_str());
      } else if (!drmConfig.system.empty()) {
        kodi::Log(ADDON_LOG_DEBUG, "DRM system %s configured but no license URL provided",
                  drmConfig.system.c_str());

        // Some DRM systems might not need a license URL
        if (drmConfig.system == "org.w3.clearkey") {
          properties.emplace_back("inputstream.adaptive.drm_legacy", drmConfig.system);
          kodi::Log(ADDON_LOG_DEBUG, "Set ClearKey DRM without license URL");
        }
      }
    }
  }

  return PVR_ERROR_NO_ERROR;
}

// Channel Group Methods
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