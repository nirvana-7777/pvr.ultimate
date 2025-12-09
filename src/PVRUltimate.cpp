#include "PVRUltimate.h"
#include <kodi/General.h>
#include <sstream>
#include <ctime>
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

bool CPVRUltimate::GetChannelInfo(int channelUid, std::string& provider,
                                   std::string& channelId, int& catchupHours) {
  auto it = m_channelLookup.find(channelUid);

  if (it == m_channelLookup.end()) {
    kodi::Log(ADDON_LOG_ERROR, "Channel lookup failed for channelUid: %d", channelUid);
    return false;
  }

  provider = it->second.provider;
  channelId = it->second.channelId;
  catchupHours = it->second.catchupHours;

  kodi::Log(ADDON_LOG_DEBUG, "Channel lookup for %d: provider=%s, channelId=%s, catchupHours=%d",
            channelUid, provider.c_str(), channelId.c_str(), catchupHours);
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

  int radioCount = 0;
  int tvCount = 0;

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

    // ==================================================================
    // SIMPLE RADIO CHANNEL DETECTION
    // ==================================================================

    // Primary detection: IsRadio boolean field
    if (channelJson.HasMember("IsRadio") && channelJson["IsRadio"].IsBool()) {
      channel.isRadio = channelJson["IsRadio"].GetBool();
    }
    // Fallback: Check ContentType
    else if (channelJson.HasMember("ContentType") && channelJson["ContentType"].IsString()) {
      std::string contentType = channelJson["ContentType"].GetString();
      channel.isRadio = (contentType == "RADIO");
    }
    // Default: TV channel
    else {
      channel.isRadio = false;
    }

    if (channel.isRadio) {
      radioCount++;
    } else {
      tvCount++;
    }

    // ==================================================================
    // CHANNEL LOOKUP INFO FOR CATCHUP SUPPORT
    // ==================================================================
    ChannelLookupInfo lookupInfo;
    lookupInfo.provider = provider;
    lookupInfo.channelId = channel.channelId;

    // Get catchup hours from backend response
    if (channelJson.HasMember("CatchupHours") && channelJson["CatchupHours"].IsInt()) {  // CHANGE
      lookupInfo.catchupHours = channelJson["CatchupHours"].GetInt();  // CHANGE
    } else {
      lookupInfo.catchupHours = 0; // No catchup support
    }

    m_channelLookup[channel.channelNumber] = lookupInfo;

    kodi::Log(ADDON_LOG_DEBUG, "Added channel lookup: %d -> %s/%s (catchup: %d hours)",  // CHANGE
              channel.channelNumber, lookupInfo.provider.c_str(),
              lookupInfo.channelId.c_str(), lookupInfo.catchupHours);

    m_channels.push_back(channel);

    kodi::Log(ADDON_LOG_DEBUG, "Loaded channel: %s (Backend#: %d, Kodi#: %d, Provider: %s, Type: %s)",
              channel.channelName.c_str(),
              channelJson.HasMember("ChannelNumber") && channelJson["ChannelNumber"].IsInt() ?
                channelJson["ChannelNumber"].GetInt() : 0,
              channel.channelNumber,
              channel.provider.c_str(),
              channel.isRadio ? "Radio" : "TV");
  }

  kodi::Log(ADDON_LOG_INFO, "Loaded %d channels from provider %s (TV: %d, Radio: %d, offset: %d)",
            static_cast<int>(channels.Size()), provider.c_str(), tvCount, radioCount, providerOffset);

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
  capabilities.SetSupportsEPG(true);
  capabilities.SetSupportsTV(true);
  capabilities.SetSupportsRadio(true);
  capabilities.SetSupportsRecordings(false);
  capabilities.SetSupportsTimers(false);
  capabilities.SetSupportsChannelGroups(true);
  capabilities.SetSupportsChannelScan(false);
  capabilities.SetHandlesInputStream(false);
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
  amount = 2; // TV group + Radio group
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetChannelGroups(bool radio,
                                          kodi::addon::PVRChannelGroupsResultSet& results) {
  if (radio) {
    // Create radio group
    kodi::addon::PVRChannelGroup radioGroup;
    radioGroup.SetIsRadio(true);
    radioGroup.SetGroupName("Radio Stations");
    results.Add(radioGroup);
  } else {
    // Create TV group
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

  std::string groupName = group.GetGroupName();
  bool isRadioGroup = group.GetIsRadio();

  for (const auto& channel : m_channels) {
    // Only add channels that match the group type (radio or TV)
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

PVR_ERROR CPVRUltimate::GetEPGForChannel(int channelUid, time_t start, time_t end,
                                         kodi::addon::PVREPGTagsResultSet& results) {
  // Find the channel
  UltimateChannel* ultimateChannel = nullptr;
  for (auto& ch : m_channels) {
    if (ch.channelNumber == channelUid) {
      ultimateChannel = &ch;
      break;
    }
  }

  if (!ultimateChannel) {
    kodi::Log(ADDON_LOG_DEBUG, "Channel not found for EPG: %d", channelUid);
    return PVR_ERROR_NO_ERROR;
  }

  kodi::Log(ADDON_LOG_DEBUG, "Fetching EPG for %s (%s) from %ld to %ld",
            ultimateChannel->channelName.c_str(), ultimateChannel->provider.c_str(),
            start, end);

  // Build URL for EPG API
  std::ostringstream url;
  url << BuildApiUrl("/api/providers/") << ultimateChannel->provider
      << "/channels/" << ultimateChannel->channelId << "/epg"
      << "?start_time=" << start << "&end_time=" << end;

  // Add country if available
  if (!ultimateChannel->country.empty()) {
    url << "&country=" << ultimateChannel->country;
  }

  std::string response = HttpGet(url.str());
  if (response.empty()) {
    kodi::Log(ADDON_LOG_DEBUG, "No EPG data returned for channel %d", channelUid);
    return PVR_ERROR_NO_ERROR;
  }

  rapidjson::Document document;
  if (!ParseJsonResponse(response, document)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to parse EPG JSON for channel %d", channelUid);
    return PVR_ERROR_NO_ERROR;
  }

  // Check for error response
  if (document.HasMember("error") && document["error"].IsString()) {
    kodi::Log(ADDON_LOG_WARNING, "EPG API error: %s", document["error"].GetString());
    return PVR_ERROR_NO_ERROR;
  }

  // Parse EPG data
  if (document.HasMember("epg") && document["epg"].IsArray()) {
    const rapidjson::Value& epgArray = document["epg"];

    for (const auto& epgItem : epgArray.GetArray()) {
      kodi::addon::PVREPGTag tag;

      // REQUIRED: Unique broadcast ID
      std::string uniqueId = std::to_string(channelUid) + "_" +
                            std::to_string(epgItem["start"].GetUint64()) + "_" +
                            std::to_string(epgItem["end"].GetUint64());
      tag.SetUniqueBroadcastId(std::hash<std::string>{}(uniqueId));

      // REQUIRED: Channel ID
      tag.SetUniqueChannelId(channelUid);

      // REQUIRED: Title
      if (epgItem.HasMember("title") && epgItem["title"].IsString()) {
        tag.SetTitle(epgItem["title"].GetString());
      } else {
        tag.SetTitle("Unknown");
      }

      // REQUIRED: Start time (UTC)
      if (epgItem.HasMember("start") && epgItem["start"].IsUint64()) {
        tag.SetStartTime(epgItem["start"].GetUint64());
      } else {
        tag.SetStartTime(0);
      }

      // REQUIRED: End time (UTC)
      if (epgItem.HasMember("end") && epgItem["end"].IsUint64()) {
        tag.SetEndTime(epgItem["end"].GetUint64());
      } else {
        tag.SetEndTime(0);
      }

      // OPTIONAL: Plot outline
      if (epgItem.HasMember("plot") && epgItem["plot"].IsString()) {
        tag.SetPlotOutline(epgItem["plot"].GetString());
      }

      // OPTIONAL: Plot/description
      if (epgItem.HasMember("description") && epgItem["description"].IsString()) {
        tag.SetPlot(epgItem["description"].GetString());
      }

      // OPTIONAL: Icon
      if (epgItem.HasMember("icon") && epgItem["icon"].IsString()) {
        tag.SetIconPath(epgItem["icon"].GetString());
      }

      // OPTIONAL: Genre
      if (epgItem.HasMember("genre") && epgItem["genre"].IsInt()) {
        tag.SetGenreType(epgItem["genre"].GetInt());
      }

      // OPTIONAL: Parental rating
      if (epgItem.HasMember("parental_rating") && epgItem["parental_rating"].IsInt()) {
        tag.SetParentalRating(epgItem["parental_rating"].GetInt());
      }

      // OPTIONAL: Episode info
      if (epgItem.HasMember("episode_number") && epgItem["episode_number"].IsInt()) {
        tag.SetEpisodeNumber(epgItem["episode_number"].GetInt());
      }

      if (epgItem.HasMember("season_number") && epgItem["season_number"].IsInt()) {
        tag.SetSeriesNumber(epgItem["season_number"].GetInt());
      }

      if (epgItem.HasMember("episode_name") && epgItem["episode_name"].IsString()) {
        tag.SetEpisodeName(epgItem["episode_name"].GetString());
      }

      // OPTIONAL: Cast - join vector into comma-separated string
      if (epgItem.HasMember("cast") && epgItem["cast"].IsArray()) {
        std::string castStr;
        for (const auto& actor : epgItem["cast"].GetArray()) {
          if (actor.IsString()) {
            if (!castStr.empty()) castStr += ", ";
            castStr += actor.GetString();
          }
        }
        if (!castStr.empty()) {
          tag.SetCast(castStr);
        }
      }

      // OPTIONAL: Directors - join vector into comma-separated string
      if (epgItem.HasMember("directors") && epgItem["directors"].IsArray()) {
        std::string directorsStr;
        for (const auto& director : epgItem["directors"].GetArray()) {
          if (director.IsString()) {
            if (!directorsStr.empty()) directorsStr += ", ";
            directorsStr += director.GetString();
          }
        }
        if (!directorsStr.empty()) {
          tag.SetDirector(directorsStr);
        }
      }

      // OPTIONAL: Writers - join vector into comma-separated string
      if (epgItem.HasMember("writers") && epgItem["writers"].IsArray()) {
        std::string writersStr;
        for (const auto& writer : epgItem["writers"].GetArray()) {
          if (writer.IsString()) {
            if (!writersStr.empty()) writersStr += ", ";
            writersStr += writer.GetString();
          }
        }
        if (!writersStr.empty()) {
          tag.SetWriter(writersStr);
        }
      }

      // OPTIONAL: Year
      if (epgItem.HasMember("year") && epgItem["year"].IsInt()) {
        tag.SetYear(epgItem["year"].GetInt());
      }

      // OPTIONAL: First aired (UTC timestamp converted to YYYY-MM-DD format)
      if (epgItem.HasMember("first_aired") && epgItem["first_aired"].IsUint64()) {
        time_t firstAiredTime = epgItem["first_aired"].GetUint64();
        struct tm* timeinfo = gmtime(&firstAiredTime);
        char buffer[11];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d", timeinfo);
        tag.SetFirstAired(buffer);
      }

      results.Add(tag);
    }

    kodi::Log(ADDON_LOG_DEBUG, "Added %zu EPG entries for channel %d",
              epgArray.Size(), channelUid);
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::IsEPGTagRecordable(const kodi::addon::PVREPGTag& tag, bool& bIsRecordable) {
  // For now, assume EPG tags are not recordable
  bIsRecordable = false;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag,
                                         bool& bIsPlayable) {
    // Default to not playable
    bIsPlayable = false;

    // Get channel info
    int channelUid = tag.GetUniqueChannelId();
    std::string provider;
    std::string channelId;
    int catchupHours = 0;

    if (!GetChannelInfo(channelUid, provider, channelId, catchupHours)) {
        kodi::Log(ADDON_LOG_DEBUG,
                  "Cannot determine playability - no channel info for %d", channelUid);
        return PVR_ERROR_NO_ERROR;
    }

    // Check if channel supports catchup
    if (catchupHours <= 0) {  // CHANGE
      kodi::Log(ADDON_LOG_DEBUG,
                "EPG not playable - channel %s/%s has no catchup (hours: %d)",  // CHANGE
                provider.c_str(), channelId.c_str(), catchupHours);
      return PVR_ERROR_NO_ERROR;
    }

    // Get current time
    time_t now = std::time(nullptr);
    time_t startTime = tag.GetStartTime();
    time_t endTime = tag.GetEndTime();

    // Calculate catchup window boundaries
    time_t catchupStart = now - (catchupHours * 3600);

    // Event must have ended (be in the past)
    if (endTime > now) {
        kodi::Log(ADDON_LOG_DEBUG,
                  "EPG not playable - event hasn't ended yet (ends in %ld seconds)",
                  endTime - now);
        return PVR_ERROR_NO_ERROR;
    }

    // Event must be within catchup window
    if (endTime < catchupStart) {
      kodi::Log(ADDON_LOG_DEBUG,
                "EPG not playable - event too old (ended %ld hours ago, max: %d hours)",  // CHANGE
                (now - endTime) / 3600, catchupHours);
      return PVR_ERROR_NO_ERROR;
    }

    // All checks passed - event is playable
    bIsPlayable = true;

    kodi::Log(ADDON_LOG_DEBUG,
              "EPG tag IS playable: '%s' on %s/%s (ended %ld mins ago, catchup: %d hours)",  // CHANGE
              tag.GetTitle().c_str(), provider.c_str(), channelId.c_str(),
              (now - endTime) / 60, catchupHours);  // CHANGE

    return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRUltimate::GetEPGTagStreamProperties(
    const kodi::addon::PVREPGTag& tag,
    std::vector<kodi::addon::PVRStreamProperty>& properties) {

    // Extract channel ID from EPG tag
    int channelUid = tag.GetUniqueChannelId();
    unsigned int broadcastId = tag.GetUniqueBroadcastId();

    kodi::Log(ADDON_LOG_INFO, "Getting EPG stream properties for channel %d, broadcast %u, "
              "EPG: '%s' (start: %ld, end: %ld)",
              channelUid, broadcastId, tag.GetTitle().c_str(),
              tag.GetStartTime(), tag.GetEndTime());

    // Get channel info (provider, channelId, catchupDays) from lookup
    std::string provider;
    std::string channelId;
    int catchupHours = 0;

    if (!GetChannelInfo(channelUid, provider, channelId, catchupHours)) {
        kodi::Log(ADDON_LOG_ERROR, "Failed to get channel info for EPG tag (channelUid: %d)",
                  channelUid);
        return PVR_ERROR_INVALID_PARAMETERS;
    }

    // Check if this channel supports catchup
    if (catchupHours <= 0) {  // CHANGE
      kodi::Log(ADDON_LOG_WARNING,
                "Channel %s/%s does not support catchup (catchupHours: %d)",  // CHANGE
                provider.c_str(), channelId.c_str(), catchupHours);
      return PVR_ERROR_NOT_IMPLEMENTED;
    }

    // Check if EPG tag is in the past (required for catchup)
    time_t now = std::time(nullptr);
    if (tag.GetStartTime() > now) {
        kodi::Log(ADDON_LOG_WARNING, "EPG tag is in the future, cannot play yet");
        return PVR_ERROR_NOT_IMPLEMENTED;
    }

    // Check if EPG tag is too old (beyond catchup window)
    time_t catchupStart = now - (catchupHours * 3600);

  if (tag.GetEndTime() < catchupStart) {
      kodi::Log(ADDON_LOG_WARNING,
                "EPG tag is outside catchup window (max %d hours)", catchupHours);  // CHANGE
      return PVR_ERROR_NOT_IMPLEMENTED;
    }

    // Retry if backend became unavailable
    if (!m_backendAvailable && !RetryBackendCall("EPG stream playback")) {
        kodi::Log(ADDON_LOG_ERROR, "Backend unavailable for EPG stream playback");
        return PVR_ERROR_SERVER_ERROR;
    }

    // Build STREAM URL (not manifest!) with time parameters for catchup
    std::ostringstream streamApiUrl;
    streamApiUrl << BuildApiUrl("/api/providers/") << provider
                 << "/channels/" << channelId << "/stream"
                 << "?start_time=" << tag.GetStartTime()
                 << "&end_time=" << tag.GetEndTime()
                 << "&epg_id=" << broadcastId;

    // Add country if available from channel
    UltimateChannel* ultimateChannel = nullptr;
    for (auto& ch : m_channels) {
        if (ch.channelNumber == channelUid) {
            ultimateChannel = &ch;
            if (!ch.country.empty()) {
                streamApiUrl << "&country=" << ch.country;
            }
            break;
        }
    }

    kodi::Log(ADDON_LOG_INFO, "Calling catchup stream API: %s", streamApiUrl.str().c_str());

    std::string response = HttpGet(streamApiUrl.str());
    if (response.empty()) {
        kodi::Log(ADDON_LOG_ERROR, "Empty response from catchup stream API");
        return PVR_ERROR_SERVER_ERROR;
    }

    // For direct mode (no proxy), backend returns JSON with manifest_url
    // For proxy mode, backend returns MPD content directly

    // Try to parse as JSON first
    rapidjson::Document document;
    bool isJson = ParseJsonResponse(response, document);

    std::string manifestUrl;

    if (isJson && document.IsObject()) {
        // JSON response - check for error or manifest_url
        if (document.HasMember("error") && document["error"].IsString()) {
            kodi::Log(ADDON_LOG_ERROR, "Catchup stream API returned error: %s",
                      document["error"].GetString());
            return PVR_ERROR_SERVER_ERROR;
        }

        if (document.HasMember("manifest_url") && document["manifest_url"].IsString()) {
            manifestUrl = document["manifest_url"].GetString();
            kodi::Log(ADDON_LOG_INFO, "Extracted catchup manifest URL: %s", manifestUrl.c_str());
        } else {
            kodi::Log(ADDON_LOG_ERROR, "No manifest_url in catchup API response");
            return PVR_ERROR_SERVER_ERROR;
        }
    } else {
        // Not JSON - assume it's direct MPD content (proxy mode)
        // Use the stream URL itself as manifest URL
        manifestUrl = streamApiUrl.str();
        kodi::Log(ADDON_LOG_INFO, "Using stream URL directly (proxy mode): %s",
                  manifestUrl.c_str());
    }

    // Set up inputstream.adaptive properties
    properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.adaptive");
    properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, manifestUrl);

    // Add catchup-specific properties for inputstream.adaptive
    properties.emplace_back("inputstream.adaptive.play_timeshift_buffer", "false");
    properties.emplace_back("inputstream.adaptive.manifest_update_parameter", "full");

    // Get DRM configuration for catchup
    if (ultimateChannel && ultimateChannel->useCdm) {
        // Build DRM URL with catchup parameters
        std::ostringstream drmUrl;
        drmUrl << BuildApiUrl("/api/providers/") << provider
               << "/channels/" << channelId << "/drm"
               << "?start_time=" << tag.GetStartTime()
               << "&end_time=" << tag.GetEndTime()
               << "&epg_id=" << broadcastId;

        if (!ultimateChannel->country.empty()) {
            drmUrl << "&country=" << ultimateChannel->country;
        }

        kodi::Log(ADDON_LOG_DEBUG, "Fetching DRM for catchup: %s", drmUrl.str().c_str());

        if (m_useModernDrm) {
            // Version 22+: Use new JSON-based DRM config format
            std::string drmResponse = HttpGet(drmUrl.str());

            if (!drmResponse.empty()) {
                rapidjson::Document drmDoc;
                if (ParseJsonResponse(drmResponse, drmDoc)) {
                    if (drmDoc.HasMember("drm_configs") && drmDoc["drm_configs"].IsObject()) {
                        // Extract just the drm_configs object
                        rapidjson::Document drmConfigs(rapidjson::kObjectType);
                        drmConfigs.CopyFrom(drmDoc["drm_configs"], drmConfigs.GetAllocator());

                        if (!drmConfigs.ObjectEmpty()) {
                            // Convert to string
                            rapidjson::StringBuffer buffer;
                            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
                            drmConfigs.Accept(writer);

                            std::string drmConfigStr = buffer.GetString();
                            properties.emplace_back("inputstream.adaptive.drm", drmConfigStr);

                            kodi::Log(ADDON_LOG_INFO,
                                      "Set modern DRM config for catchup (%zu bytes)",
                                      drmConfigStr.size());
                        }
                    }
                }
            }
        } else {
            // Legacy version (<22): Use old drm_legacy format
            std::string drmResponse = HttpGet(drmUrl.str());

            if (!drmResponse.empty()) {
                rapidjson::Document drmDoc;
                if (ParseJsonResponse(drmResponse, drmDoc)) {
                    if (drmDoc.HasMember("drm_configs") && drmDoc["drm_configs"].IsObject()) {
                        const rapidjson::Value& drmConfigs = drmDoc["drm_configs"];

                        // Get first DRM system
                        for (auto it = drmConfigs.MemberBegin();
                             it != drmConfigs.MemberEnd(); ++it) {

                            std::string drmSystem = it->name.GetString();
                            const rapidjson::Value& drmData = it->value;

                            if (drmData.HasMember("license") &&
                                drmData["license"].IsObject()) {
                                const rapidjson::Value& license = drmData["license"];

                                if (license.HasMember("server_url") &&
                                    license["server_url"].IsString()) {

                                    std::string licenseUrl = license["server_url"].GetString();
                                    std::string drmLegacy = drmSystem + "|" + licenseUrl;

                                    // Add headers if present
                                    if (license.HasMember("req_headers") &&
                                        license["req_headers"].IsString()) {
                                        drmLegacy += "|" +
                                                    std::string(license["req_headers"].GetString());
                                    }

                                    properties.emplace_back("inputstream.adaptive.drm_legacy",
                                                          drmLegacy);

                                    kodi::Log(ADDON_LOG_INFO,
                                              "Set legacy DRM config for catchup: %s",
                                              drmLegacy.c_str());
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    kodi::Log(ADDON_LOG_INFO,
              "Successfully set up catchup stream for '%s' (channel: %s, program: %s)",
              ultimateChannel ? ultimateChannel->channelName.c_str() : "unknown",
              channelId.c_str(), tag.GetTitle().c_str());

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