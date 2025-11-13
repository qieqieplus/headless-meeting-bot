#ifndef HEADLESS_ZOOM_BOT_MEETING_H
#define HEADLESS_ZOOM_BOT_MEETING_H

#include <functional>
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
#include "events/MeetingReminderEvent.h"
#include "events/MeetingServiceEvent.h"

// Forward declarations
struct VideoEncoderConfig;
struct HlsMuxerConfig;
class UserController;
struct UserStatusEvent;

class Meeting {
 private:
  MeetingConfig config_;

  // Service references (injected instead of obtained from singleton)
  ZOOMSDK::IMeetingService* meeting_service_;
  ZOOMSDK::ISettingService* setting_service_;

  bool is_joined_;

  // Media controller (encapsulates all audio/video handling)
  std::unique_ptr<MediaController> media_controller_;
  std::unique_ptr<UserController> user_controller_;

  // Event object ownership
  std::unique_ptr<MeetingReminderEvent> reminder_event_;
  std::unique_ptr<MeetingServiceEvent> meeting_service_event_;

  ZOOMSDK::SDKError SetupMeetingEvents();
  void MuteMyself();
  void EnableAudio() const;

 public:
  Meeting(const MeetingConfig& config, ZOOMSDK::IMeetingService* meeting_service,
          ZOOMSDK::ISettingService* setting_service);
  ~Meeting() noexcept;

  // Rule of Five: delete copy operations, implement move operations
  Meeting(const Meeting&) = delete;
  Meeting& operator=(const Meeting&) = delete;
  Meeting(Meeting&&) noexcept;
  Meeting& operator=(Meeting&&) noexcept;

  ZOOMSDK::SDKError Join();
  ZOOMSDK::SDKError Start();
  ZOOMSDK::SDKError Leave();

  ZOOMSDK::SDKError StartOrJoin();

  bool IsMeetingStart() const;
  ZOOMSDK::IMeetingService* GetMeetingService() const { return meeting_service_; }

  // Media controller access (for C API routing and delegate setup)
  MediaController* GetMediaController() { return media_controller_.get(); }
  UserController* GetUserController() { return user_controller_.get(); }

  void SetOnMeetingStatusChanged(const std::function<void(ZOOMSDK::MeetingStatus, int)>& cb);

  // Static factory methods
  static std::unique_ptr<Meeting> CreateMeeting(const MeetingConfig& meeting_config,
                                                ZOOMSDK::IMeetingService* meeting_service,
                                                ZOOMSDK::ISettingService* setting_service);
  static std::unique_ptr<Meeting> CreateMeeting(
      const std::string& meeting_id, const std::string& password, const std::string& display_name,
      bool is_meeting_start, const std::string& join_token, bool use_raw_audio, bool use_raw_video,
      ZOOMSDK::IMeetingService* meeting_service, ZOOMSDK::ISettingService* setting_service);
};

#endif  // HEADLESS_ZOOM_BOT_MEETING_H
