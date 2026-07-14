#pragma once

#include <string>

struct UltimateProvider {
  std::string name;
  std::string label;
  std::string country;
  std::string logo;
  bool enabled = true;
  int uniqueId = 0;
};

struct UltimateChannel {
  std::string uniqueId;
  int channelNumber = 0;
  std::string channelName;
  std::string iconPath;
  std::string provider;
  std::string channelId;
  bool isRadio = false;

  std::string mode;
  bool sessionManifest = false;
  std::string manifest;
  std::string manifestScript;
  bool useCdm = true;
  std::string cdmMode;
  std::string contentType;
  std::string country;
  std::string language;
  std::string streamingFormat;
};

struct DRMLicense {
  std::string serverUrl;
  std::string serverCertificate;
  std::string reqHeaders;
  std::string reqData;
  std::string reqParams;
  bool useHttpGetRequest = false;
  std::string wrapper;
  std::string unwrapper;
};

struct DRMConfig {
  std::string system;
  int priority = 1;
  DRMLicense license;
};

struct ChannelLookupInfo {
  std::string provider;
  std::string channelId;
  int catchupHours = 0;
};

struct UltimateRecording {
  std::string uniqueId;
  std::string title;
  std::string provider;

  std::string channelName;
  int channelUid = 0;
  bool isRadio = false;

  time_t startTime = 0;
  time_t endTime = 0;
  int durationSeconds = 0;
  std::string firstAired;

  int seasonNumber = 0;
  int episodeNumber = 0;
  std::string episodeName;
  std::string seriesTitle;
  std::string seriesId;

  std::string plot;
  std::string plotOutline;
  std::string genreDescription;
  std::string genre;
  int genreType = 0;
  int genreSubType = 0;

  std::string iconPath;
  std::string thumbnailUrl;
  std::string fanartUrl;

  int playCount = 0;
  int lastPlayedPosition = 0;

  std::string directory;
  int sizeInBytes = 0;
  int priority = 0;
  int lifetime = 0;
  std::string flags;
  int clientProviderUid = 0;
  std::string providerName;

  int epgEventId = 0;

  std::string status;
  bool isDeleted = false;
  bool isPlayable = false;
  int releaseYear = 0;
};

struct UltimateTimerType {
  int id = 0;
  std::string description;
  int priority = 50;
};

struct UltimateTimer {
  int clientIndex = 0;
  std::string provider;
  int timerTypeId = 0;
  std::string title;

  int parentClientIndex = 0;

  int clientChannelUid = 0;
  std::string channelName;

  time_t startTime = 0;
  time_t endTime = 0;
  bool startAnyTime = false;
  bool endAnyTime = false;
  time_t firstDay = 0;

  int marginStart = 0;
  int marginEnd = 0;

  std::string epgSearchString;
  bool fullTextEpgSearch = false;
  int epgUid = 0;
  std::string epgEventId;

  int weekdays = 0;
  int preventDuplicateEpisodes = 0;
  std::string seriesLink;

  std::string directory;
  int priority = 0;
  int lifetime = 0;
  int maxRecordings = 0;
  int recordingGroup = 0;

  int genreType = 0;
  int genreSubType = 0;

  int state = 0;
  std::string description;
  time_t lastUpdated = 0;
};