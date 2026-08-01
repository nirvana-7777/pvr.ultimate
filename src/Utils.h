#pragma once

#include <string>
#include <nlohmann/json.hpp>

class Utils {
public:
    static bool ParseJsonResponse(const std::string& response, nlohmann::json& document);
    static std::string Base64Decode(const std::string& base64Data);
    static std::string UrlEncode(const std::string& value);
    static std::string ConvertDrmJsonToLegacy(const nlohmann::json& drmJson);
    static time_t ParseISO8601(const std::string& isoString);
    static std::string ToISO8601(time_t time);
    static int GenerateProviderUniqueId(const std::string& providerName);

    // URL path safe - escapes path components, preserves slashes.
    // Restored from pre-migration Utils: provider/channel/recording IDs are
    // interpolated directly into URL paths throughout the managers, so any
    // ID containing spaces, '/', or non-ASCII characters would otherwise
    // break the request or corrupt the path.
    static std::string UrlPathEncode(const std::string& value);

    // Redact sensitive data (tokens/keys/passwords/basic-auth) from URLs before
    // logging. Restored from pre-migration Utils - without this, API keys and
    // Authorization headers embedded in request URLs are written to the Kodi
    // log in cleartext.
    static std::string RedactUrl(const std::string& url);

    // Safe integer parsing from string (no exceptions).
    static int SafeStoi(const std::string& str, int defaultValue = 0);
};