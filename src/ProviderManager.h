#pragma once

#include "Models.h"
#include <kodi/addon-instance/PVR.h>
#include <vector>
#include <map>
#include <shared_mutex>
#include <mutex>
#include <functional>
#include <nlohmann/json.hpp>

class ProviderManager {
public:
    ProviderManager() = default;

    bool LoadProviders(const std::function<std::string(const std::string&)>& httpGet,
                       const std::function<bool(const std::string&, nlohmann::json&)>& parseJson);

    bool GetProviders(kodi::addon::PVRProvidersResultSet& results) const;
    int GetProvidersAmount() const;

    std::string GetProviderName(int uniqueId) const;
    const std::vector<UltimateProvider>& GetProviders() const { return m_providers; }

    void LockShared() const { m_dataMutex.lock_shared(); }
    void UnlockShared() const { m_dataMutex.unlock_shared(); }
    void LockUnique() const { m_dataMutex.lock(); }
    void UnlockUnique() const { m_dataMutex.unlock(); }

private:
    std::vector<UltimateProvider> m_providers;
    std::map<std::string, int> m_providerIdMap;
    mutable std::shared_mutex m_dataMutex;
};