#pragma once

#include "Models.h"
#include <kodi/addon-instance/PVR.h>
#include <vector>
#include <map>
#include <set>
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
    // Thread-safe snapshot (copy taken under the lock) for UI / reload code.
    std::vector<UltimateProvider> GetProvidersCopy() const;

    // --- Local (client-side) provider selection -------------------------------
    // Purely a sub-selection of what the backend offers; the backend is never
    // touched. Stored as the set of providers the user DESELECTED, so providers
    // that newly appear on the backend are active by default.
    void LoadLocalSelection(const std::string& filePath);
    std::set<std::string> GetLocallyDisabled() const;
    // True if a valid selection file was read or a selection was stored this session.
    // False on a fresh install (-> first-start selection dialog).
    bool HasLocalSelection() const;
    // Stores and persists the selection; returns false if the file could not be written.
    // Takes effect on the next LoadProviders().
    bool SetLocallyDisabled(const std::set<std::string>& disabled);

    void LockShared() const { m_dataMutex.lock_shared(); }
    void UnlockShared() const { m_dataMutex.unlock_shared(); }
    void LockUnique() const { m_dataMutex.lock(); }
    void UnlockUnique() const { m_dataMutex.unlock(); }

private:
    std::vector<UltimateProvider> m_providers;
    std::map<std::string, int> m_providerIdMap;
    mutable std::shared_mutex m_dataMutex;
    std::set<std::string> m_locallyDisabled;
    std::string m_selectionPath;
    bool m_hasSelection = false;
};