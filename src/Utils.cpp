#include "Utils.h"
#include <kodi/General.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <vector>
#include <ctime>

#ifdef _WIN32
    #define timegm _mkgmtime
#endif

bool Utils::ParseJsonResponse(const std::string& response, nlohmann::json& document) {
  if (response.empty()) return false;
  try {
    document = nlohmann::json::parse(response);
    return true;
  } catch (const nlohmann::json::parse_error& e) {
    kodi::Log(ADDON_LOG_ERROR, "JSON parse error: %s (byte %zu)", e.what(), e.byte);
    return false;
  } catch (const std::exception& e) {
    kodi::Log(ADDON_LOG_ERROR, "Unexpected JSON error: %s", e.what());
    return false;
  }
}

std::string Utils::Base64Decode(const std::string& base64Data) {
  static const std::string base64_chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz"
      "0123456789+/";

  std::string ret;
  std::vector<int> T(256, -1);
  for (int i = 0; i < 64; i++) T[base64_chars[i]] = i;

  int val = 0, valb = -8;
  for (unsigned char c : base64Data) {
    if (T[c] == -1) break;
    val = (val << 6) + T[c];
    valb += 6;
    if (valb >= 0) {
      ret.push_back(char((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return ret;
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

std::string Utils::ConvertDrmJsonToLegacy(const nlohmann::json& drmJson) {
  if (!drmJson.is_object()) return "";

  // Always select by backend-declared "priority" (lowest wins, ties broken by
  // key name for determinism) - matches PVRUltimate::GetDRMConfig/GetDRMConfigJson
  // elsewhere in this file. Previously this function special-cased
  // "com.widevine.alpha" to always win when present, silently ignoring the
  // backend's priority field whenever Widevine was offered as an option.
  std::string keySystem;
  const nlohmann::json* drmSystem = nullptr;

  if (!drmJson.empty()) {
    std::vector<std::pair<std::string, int>> drmPriorities;
    for (auto it = drmJson.begin(); it != drmJson.end(); ++it) {
      int priority = 1;
      if (it.value().is_object() && it.value().contains("priority") &&
          it.value()["priority"].is_number_integer()) {
        priority = it.value()["priority"].get<int>();
      }
      drmPriorities.push_back({it.key(), priority});
    }
    std::sort(drmPriorities.begin(), drmPriorities.end(),
              [](const auto& a, const auto& b) {
                if (a.second != b.second) return a.second < b.second;
                return a.first < b.first;
              });
    if (!drmPriorities.empty()) {
      keySystem = drmPriorities.front().first;
      drmSystem = &drmJson[keySystem];
    }
  }

  if (!drmSystem || !drmSystem->is_object()) return "";
  if (!drmSystem->contains("license") || !(*drmSystem)["license"].is_object()) return "";

  const nlohmann::json& license = (*drmSystem)["license"];

  std::string licenseUrl, headers, reqData;
  if (license.contains("server_url") && license["server_url"].is_string())
    licenseUrl = license["server_url"].get<std::string>();
  if (license.contains("req_headers") && license["req_headers"].is_string())
    headers = license["req_headers"].get<std::string>();
  if (license.contains("req_data") && license["req_data"].is_string())
    reqData = license["req_data"].get<std::string>();

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
  // Force the classic "C" locale so this is safe to call from any thread
  // regardless of the process-global locale (get_time's month/day-name
  // parsing is locale-sensitive; numeric-only formats like this one are less
  // exposed, but imbuing classic() removes the dependency entirely and keeps
  // this call thread-safe under kodi's locale handling).
  ss.imbue(std::locale::classic());
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

namespace {
inline bool IsUrlSafe(char c) {
  return (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') ||
         c == '-' || c == '_' || c == '.' || c == '~';
}
}  // namespace

std::string Utils::UrlPathEncode(const std::string& value) {
  std::ostringstream escaped;
  escaped.fill('0');
  escaped << std::hex;
  for (unsigned char c : value) {
    if (IsUrlSafe(static_cast<char>(c)) || c == '/') {
      escaped << static_cast<char>(c);
    } else {
      escaped << std::uppercase;
      escaped << '%' << std::setw(2) << static_cast<int>(c);
      escaped << std::nouppercase;
    }
  }
  return escaped.str();
}

std::string Utils::RedactUrl(const std::string& url) {
  std::string result = url;
  std::vector<std::string> patterns = {"token=", "key=", "api_key=", "password=", "apikey=", "auth=", "Authorization="};

  for (const auto& pattern : patterns) {
    size_t pos = result.find(pattern);
    while (pos != std::string::npos) {
      bool isParam = (pos == 0 || result[pos - 1] == '?' || result[pos - 1] == '&' ||
                      result[pos - 1] == '|' || result[pos - 1] == '=');
      if (isParam) {
        size_t end = result.find_first_of("&|", pos);
        if (end == std::string::npos) end = result.length();
        result.replace(pos, end - pos, pattern + "REDACTED");
      }
      pos = result.find(pattern, pos + 1);
    }
  }

  size_t authPos = result.find("://");
  if (authPos != std::string::npos) {
    size_t start = authPos + 3;
    size_t atPos = result.find("@", start);
    if (atPos != std::string::npos) {
      size_t colonPos = result.find(":", start);
      if (colonPos != std::string::npos && colonPos < atPos) {
        result.replace(colonPos + 1, atPos - colonPos - 1, "REDACTED");
      }
    }
  }

  return result;
}

int Utils::SafeStoi(const std::string& str, int defaultValue) {
  if (str.empty()) return defaultValue;
  try {
    size_t pos = 0;
    int value = std::stoi(str, &pos);
    return value;
  } catch (...) {
    return defaultValue;
  }
}