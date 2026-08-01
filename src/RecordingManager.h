#pragma once

#include "Models.h"
#include <kodi/addon-instance/PVR.h>
#include <vector>
#include <shared_mutex>
#include <mutex>
#include <functional>
#include <string>
#include <set>
#include <nlohmann/json.hpp>

class RecordingManager {
public:
  RecordingManager() = default;

  bool LoadRecordings(const std::vector<UltimateProvider>& providers,
                      const std::function<std::string(const std::string&)>& httpGet,
                      const std::function<bool(const std::string&, nlohmann::json&)>& parseJson);

  int GetRecordingsAmount(bool deleted) const;
  bool GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results) const;

  bool DeleteRecording(const std::string& recordingId,
                       const std::function<std::string(const std::string&)>& buildApiUrl,
                       const std::function<bool(const std::string&)>& httpDelete);

  // drmConfigsBase64/streamHeadersBase64 are populated from the manifest
  // response when supportsPiggyback is true, so the caller can apply DRM and
  // stream headers via ApplyDRMProperties/ApplyStreamHeaders - the same
  // out-param contract EPGManager::GetEPGTagStreamProperties already uses.
  // Previously these were only fetched into locals inside this function and
  // discarded, so DRM-protected recordings played back without a license
  // config whenever the piggyback path was used.
  bool GetRecordingStreamProperties(const std::string& recordingId,
                                    std::vector<kodi::addon::PVRStreamProperty>& properties,
                                    const std::function<std::string(const std::string&)>& buildApiUrl,
                                    const std::function<std::string(const std::string&)>& httpGet,
                                    const std::function<bool(const std::string&, nlohmann::json&)>& parseJson,
                                    const std::function<bool(const std::string&, std::string&, std::string&, std::string&)>& httpGetWithHeaders,
                                    bool supportsPiggyback,
                                    std::string& drmConfigsBase64,
                                    std::string& streamHeadersBase64);

  static bool GetRecordingEdl(const std::string& recordingId, std::vector<kodi::addon::PVREDLEntry>& edl);

  UltimateRecording* FindRecording(const std::string& recordingId);
  const std::vector<UltimateRecording>& GetRecordings() const { return m_recordings; }

  void LockShared() const { m_dataMutex.lock_shared(); }
  void UnlockShared() const { m_dataMutex.unlock_shared(); }
  void LockUnique() const { m_dataMutex.lock(); }
  void UnlockUnique() const { m_dataMutex.unlock(); }

private:
  static void LoadRecordingsForProvider(const std::string& provider,
                                        const std::function<std::string(const std::string&)>& httpGet,
                                        const std::function<bool(const std::string&, nlohmann::json&)>& parseJson,
                                        std::vector<UltimateRecording>& outRecordings);

  static bool MapRecordingToKodi(const UltimateRecording& recording, kodi::addon::PVRRecording& kodiRecording);

  std::vector<UltimateRecording> m_recordings;
  mutable std::shared_mutex m_dataMutex;
  
  static const std::set<std::string> PLAYABLE_STATUSES;
};