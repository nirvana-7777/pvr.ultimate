#include "Utils.h"
#include <kodi/General.h>
#include <kodi/tools/StringUtils.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

#ifdef _WIN32
#include <time.h>
#else
#include <time.h>
#endif

bool Utils::ParseJsonResponse(const std::string& response, rapidjson::Document& document) {
  rapidjson::ParseResult result = document.Parse(response.c_str());
  if (result.IsError()) {
    kodi::Log(ADDON_LOG_ERROR, "JSON parse error: %s (Offset: %zu)",
              rapidjson::GetParseError_En(result.Code()), result.Offset());
    return false;
  }
  return true;
}

std::string Utils::Base64Decode(const std::string& base64Data) {
  return kodi::tools::StringUtils::Base64Decode(base64Data);
}

std::string Utils::UrlEncode(const std::string& value) {
  std::ostringstream escaped;
  escaped.fill('0');
  escaped << std::hex;
  for (char c : value) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      escaped << c;
    } else {
      escaped << std::uppercase;
      escaped << '%' << std::setw(2) << int((unsigned char)c);
      escaped << std::nouppercase;
    }
  }
  return escaped.str();
}

std::string Utils::ConvertDrmJsonToLegacy(const rapidjson::Value& drmJson) {
  if (!drmJson.IsObject()) return "";

  std::string keySystem = "com.widevine.alpha";
  const rapidjson::Value* drmSystem = nullptr;

  if (drmJson.HasMember("com.widevine.alpha")) {
    drmSystem = &drmJson["com.widevine.alpha"];
  } else if (drmJson.MemberBegin() != drmJson.MemberEnd()) {
    std::vector<std::pair<std::string, int>> drmPriorities;
    for (auto it = drmJson.MemberBegin(); it != drmJson.MemberEnd(); ++it) {
      int priority = 1;
      if (it->value.HasMember("priority") && it->value["priority"].IsInt()) {
        priority = it->value["priority"].GetInt();
      }
      drmPriorities.push_back({it->name.GetString(), priority});
    }
    auto selected = std::min_element(drmPriorities.begin(), drmPriorities.end(),
                                     [](const auto& a, const auto& b) {
                                       return a.second < b.second;
                                     });
    if (selected != drmPriorities.end()) {
      keySystem = selected->first;
      drmSystem = &drmJson[keySystem];
    }
  }

  if (!drmSystem || !drmSystem->IsObject()) return "";
  if (!drmSystem->HasMember("license") || !(*drmSystem)["license"].IsObject()) return "";

  const rapidjson::Value& license = (*drmSystem)["license"];

  std::string licenseUrl, headers, reqData;
  if (license.HasMember("server_url") && license["server_url"].IsString())
    licenseUrl = license["server_url"].GetString();
  if (license.HasMember("req_headers") && license["req_headers"].IsString())
    headers = license["req_headers"].GetString();
  if (license.HasMember("req_data") && license["req_data"].IsString())
    reqData = license["req_data"].GetString();

  std::string result = keySystem;
  if (!licenseUrl.empty()) result += "|" + licenseUrl;
  if (!headers.empty())    result += "|" + headers;
  if (!reqData.empty())    result += "|" + reqData;

  return result;
}

time_t Utils::ParseISO8601(const std::string& isoString) {
  if (isoString.empty()) return 0;

  std::string clean = isoString;

  size_t dotPos = clean.find('.');
  if (dotPos != std::string::npos) {
    size_t tzPos = clean.find_first_of("Z+-", dotPos);
    if (tzPos != std::string::npos) {
      clean = clean.substr(0, dotPos) + clean.substr(tzPos);
    } else {
      clean = clean.substr(0, dotPos);
    }
  }

  std::tm tm = {};
  std::istringstream ss(clean);
  ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
  if (ss.fail()) return 0;

  time_t time = timegm(&tm);

  char zone;
  if (ss >> zone) {
    if (zone == 'Z') {
      return time;
    } else if (zone == '+' || zone == '-') {
      int zh = 0, zm = 0;
      // Field width limits each extraction to 2 digits so both "+02:00"
      // and "+0200" (no colon) are parsed correctly. Without the width,
      // "+0200" would be read entirely into zh (as 200) leaving zm
      // uninitialized/unset, producing a wildly wrong offset.
      ss >> std::setw(2) >> zh;
      if (ss.peek() == ':') ss.ignore(1);
      ss >> std::setw(2) >> zm;
      int offset = zh * 3600 + zm * 60;
      if (zone == '+') time -= offset;
      else time += offset;
    }
  }
  return time;
}

std::string Utils::ToISO8601(time_t time) {
  struct tm tm;
  #ifdef _WIN32
    gmtime_s(&tm, &time);
  #else
    gmtime_r(&time, &tm);
  #endif
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return std::string(buffer);
}

int Utils::GenerateProviderUniqueId(const std::string& providerName) {
  unsigned int hash = 5381;
  for (char c : providerName) {
    hash = ((hash << 5) + hash) + static_cast<unsigned int>(c);
  }
  return static_cast<int>(hash & 0x7FFFFFFF);
}