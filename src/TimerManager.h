#pragma once

#include "Models.h"
#include <kodi/addon-instance/PVR.h>
#include <vector>
#include <map>
#include <shared_mutex>
#include <mutex>
#include <functional>
#include <string>
#include "rapidjson/document.h"

class TimerManager {
public:
  TimerManager() = default;

  bool LoadTimerTypes(const std::vector<UltimateProvider>& providers,
                      const std::function<std::string(const std::string&)>& httpGet,
                      const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson);

  bool LoadTimers(const std::vector<UltimateProvider>& providers,
                  const std::function<std::string(const std::string&)>& httpGet,
                  const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson);

  bool GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types) const;
  int GetTimersAmount() const;
  bool GetTimers(kodi::addon::PVRTimersResultSet& results) const;

  static bool AddTimer(const kodi::addon::PVRTimer& timer,
                       const std::vector<UltimateProvider>& providers,
                       const std::map<int, ChannelLookupInfo>& channelLookup,
                       const std::function<std::string(const std::string&)>& buildApiUrl,
                       const std::function<bool(const std::string&, const std::string&)>& httpPost,
                       const std::function<void()>& loadTimers);

  bool DeleteTimer(int clientIndex, bool forceDelete,
                   const std::function<std::string(const std::string&)>& buildApiUrl,
                   const std::function<bool(const std::string&)>& httpDelete,
                   const std::function<void()>& loadTimers);

  bool UpdateTimer(const kodi::addon::PVRTimer& timer,
                   const std::function<std::string(const std::string&)>& buildApiUrl,
                   const std::function<bool(const std::string&, const std::string&)>& httpPut,
                   const std::function<void()>& loadTimers);

  UltimateTimer* FindTimer(int clientIndex);
  const std::vector<UltimateTimer>& GetTimers() const { return m_timers; }
  const std::vector<UltimateTimerType>& GetTimerTypes() const { return m_timerTypes; }

  void LockShared() const { m_dataMutex.lock_shared(); }
  void UnlockShared() const { m_dataMutex.unlock_shared(); }
  void LockUnique() const { m_dataMutex.lock(); }
  void UnlockUnique() const { m_dataMutex.unlock(); }

private:
  static void LoadTimerTypesForProvider(const std::string& provider,
                                        const std::function<std::string(const std::string&)>& httpGet,
                                        const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson,
                                        std::vector<UltimateTimerType>& outTimerTypes);

  static void LoadTimersForProvider(const std::string& provider,
                                    const std::function<std::string(const std::string&)>& httpGet,
                                    const std::function<bool(const std::string&, rapidjson::Document&)>& parseJson,
                                    std::vector<UltimateTimer>& outTimers);

  static bool MapTimerToKodi(const UltimateTimer& timer, kodi::addon::PVRTimer& kodiTimer);

  static bool MapKodiTimerToUltimate(const kodi::addon::PVRTimer& kodiTimer, UltimateTimer& ultimateTimer);

  static PVR_TIMER_STATE MapTimerStateToKodi(int state);

  std::vector<UltimateTimer> m_timers;
  std::vector<UltimateTimerType> m_timerTypes;
  mutable std::shared_mutex m_dataMutex;
};