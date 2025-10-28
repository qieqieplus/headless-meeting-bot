#ifndef HEADLESS_ZOOM_BOT_MEETING_H
#define HEADLESS_ZOOM_BOT_MEETING_H

#include <memory>
#include <string>

#include "MediaController.h"
#include "MeetingConfig.h"

// SDK interfaces needed by implementation
#include "meeting_service_components/meeting_audio_interface.h"
#include "meeting_service_components/meeting_participants_ctrl_interface.h"
#include "meeting_service_components/meeting_sharing_interface.h"
#include "meeting_service_components/meeting_video_interface.h"
#include "meeting_service_interface.h"
#include "setting_service_interface.h"

// Raw data helpers
#include "rawdata/rawdata_audio_helper_interface.h"
#include "rawdata/rawdata_renderer_interface.h"
#include "rawdata/zoom_rawdata_api.h"

// Event implementations
#include "events/MeetingRecordingCtrlEvent.h"
#include "events/MeetingReminderEvent.h"
#include "events/MeetingServiceEvent.h"
#include "events/MeetingShareEvent.h"

// Forward declarations
struct VideoEncoderConfig;
struct HlsMuxerConfig;

class Meeting {

private:
  MeetingConfig m_config;

  bool m_isJoined;
  bool m_isRecording;

  // Service references (injected instead of obtained from singleton)
  ZOOMSDK::IMeetingService *m_meetingService;
  ZOOMSDK::ISettingService *m_settingService;

  // Event object ownership
  std::unique_ptr<MeetingReminderEvent> m_reminderEvent;
  std::unique_ptr<MeetingRecordingCtrlEvent> m_recordingEvent;
  std::unique_ptr<MeetingServiceEvent> m_meetingServiceEvent;
  std::unique_ptr<MeetingShareEvent> m_shareEvent;

  // Media controller (encapsulates all audio/video handling)
  std::unique_ptr<MediaController> m_mediaController;

  ZOOMSDK::SDKError setupMeetingEvents();

public:
  Meeting(const MeetingConfig &config, ZOOMSDK::IMeetingService *meetingService,
          ZOOMSDK::ISettingService *settingService);
  ~Meeting();

  ZOOMSDK::SDKError join();
  ZOOMSDK::SDKError start();
  ZOOMSDK::SDKError leave();

  ZOOMSDK::SDKError startOrJoin();
  ZOOMSDK::SDKError startRawRecording();
  ZOOMSDK::SDKError stopRawRecording();

  bool isMeetingStart() const;
  bool isJoined() const { return m_isJoined; }
  bool isRecording() const { return m_isRecording; }

  const MeetingConfig &getConfig() const { return m_config; }
  ZOOMSDK::IMeetingService *getMeetingService() const {
    return m_meetingService;
  }

  // Media controller access (for C API routing and delegate setup)
  MediaController *getMediaController() { return m_mediaController.get(); }

  // Static factory methods
  static Meeting *createMeeting(const MeetingConfig &meetingConfig,
                                ZOOMSDK::IMeetingService *meetingService,
                                ZOOMSDK::ISettingService *settingService);
  static Meeting *
  createMeeting(const std::string &meetingId, const std::string &password,
                const std::string &displayName, bool isMeetingStart,
                const std::string &joinToken, bool useRawAudio,
                bool useRawVideo, ZOOMSDK::IMeetingService *meetingService,
                ZOOMSDK::ISettingService *settingService);

  // Static utility methods
  static bool hasError(ZOOMSDK::SDKError e, const std::string &action = "");
};

#endif // HEADLESS_ZOOM_BOT_MEETING_H
