#pragma once

#include <string>
#include "rapidjson/document.h"

class Utils {
public:
    static bool ParseJsonResponse(const std::string& response, rapidjson::Document& document);
    static std::string Base64Decode(const std::string& base64Data);
    static std::string UrlEncode(const std::string& value);
    static std::string ConvertDrmJsonToLegacy(const rapidjson::Value& drmJson);
    static time_t ParseISO8601(const std::string& isoString);
    static std::string ToISO8601(time_t time);
    static int GenerateProviderUniqueId(const std::string& providerName);
};