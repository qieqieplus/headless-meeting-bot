#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "events/IUserEventSink.h"
#include "meeting_service_components/meeting_audio_interface.h"
#include "meeting_service_components/meeting_participants_ctrl_interface.h"
#include "meeting_service_components/meeting_sharing_interface.h"
#include "meeting_service_components/meeting_video_interface.h"

namespace ZOOMSDK {
class IMeetingService;
}

class MeetingAudioEvent;
class MeetingVideoEvent;
class MeetingParticipantsEvent;
class MeetingShareEvent;

struct UserSnapshot {
  unsigned int id;
  std::string name;
  std::string avatar;
  bool audio_on;
  bool video_on;
  bool sharing;
};

struct UserStatusEvent {
  enum class Type {
    kSnapshot,
    kJoined,
    kLeft,
    kAudioMuted,
    kAudioUnmuted,
    kVideoOn,
    kVideoOff,
    kShareStarted,
    kShareStopped,
    kActiveSpeaking,
    kInactiveSpeaking
  };

  Type type;
  UserSnapshot snapshot;
  std::optional<ZOOMSDK::AudioStatus> audio_status;
  std::optional<ZOOMSDK::VideoStatus> video_status;
  std::optional<bool> share_status;
  uint64_t timestamp_ms = 0;  // Media timeline timestamp for event alignment
};

// Forward declaration
class MediaController;

class UserController : public IUserEventSink {
 public:
  explicit UserController(ZOOMSDK::IMeetingService* meeting_service);
  ~UserController() noexcept override;

  // Set MediaController for timeline clock access
  void SetMediaController(MediaController* media_controller) {
    media_controller_ = media_controller;
  }

  std::vector<UserSnapshot> GetUsers() const;
  std::optional<UserSnapshot> GetUser(unsigned int user_id) const;

  // Returns all current share sources available to view (across all sharers)
  std::vector<ZOOMSDK::ZoomSDKSharingSourceInfo> GetActiveShareSources() const;

  void SetOnUserJoin(const std::function<void(unsigned int)>& cb);
  void SetOnUserLeave(const std::function<void(unsigned int)>& cb);
  void SetOnAudioStatusChanged(const std::function<void(unsigned int, ZOOMSDK::AudioStatus)>& cb);
  void SetOnVideoStatusChanged(const std::function<void(unsigned int, ZOOMSDK::VideoStatus)>& cb);
  void SetOnShareStatusChanged(const std::function<void(unsigned int, bool)>& cb);
  void SetOnStatusEvent(const std::function<void(const UserStatusEvent&)>& cb);

  // refresh users and active share sources.
  void InitializeState();

  void OnShareStart(const ZOOMSDK::ZoomSDKSharingSourceInfo& info);
  void OnShareEnd(const ZOOMSDK::ZoomSDKSharingSourceInfo& info);

  // IUserEventSink implementation
  void HandleParticipantJoin(unsigned int user_id);
  void HandleParticipantLeft(unsigned int user_id);
  void HandleAudioStatus(unsigned int user_id, ZOOMSDK::AudioStatus status);
  void HandleVideoStatus(unsigned int user_id, ZOOMSDK::VideoStatus status);
  void HandleSpeakingStatus(unsigned int user_id, bool is_speaking);

 private:
  void HandleShareStatus(unsigned int user_id, bool is_sharing);

  void EnsureUserCached(unsigned int user_id);

  UserSnapshot MakeSnapshot(ZOOMSDK::IUserInfo* user_info) const;
  bool UpdateUser(unsigned int user_id, const std::function<bool(UserSnapshot&)>& mutator);
  void EmitStatusEvent(UserStatusEvent::Type type, const UserSnapshot& snapshot,
                       std::optional<ZOOMSDK::AudioStatus> audio = std::nullopt,
                       std::optional<ZOOMSDK::VideoStatus> video = std::nullopt,
                       std::optional<bool> share = std::nullopt);

  ZOOMSDK::IMeetingService* meeting_service_;
  ZOOMSDK::IMeetingParticipantsController* participants_ctrl_;
  ZOOMSDK::IMeetingAudioController* audio_ctrl_;
  ZOOMSDK::IMeetingVideoController* video_ctrl_;
  ZOOMSDK::IMeetingShareController* share_ctrl_;

  MediaController* media_controller_;

  std::unique_ptr<MeetingParticipantsEvent> participants_event_;
  std::unique_ptr<MeetingAudioEvent> audio_event_;
  std::unique_ptr<MeetingVideoEvent> video_event_;
  std::unique_ptr<MeetingShareEvent> share_event_;

  mutable std::mutex mutex_;
  std::unordered_map<unsigned int, UserSnapshot> users_;
  std::unordered_set<unsigned int> sharing_users_;
  std::vector<ZOOMSDK::ZoomSDKSharingSourceInfo> active_share_sources_;
  unsigned int self_user_id_ = 0;

  std::function<void(unsigned int)> on_user_join_;
  std::function<void(unsigned int)> on_user_leave_;
  std::function<void(unsigned int, ZOOMSDK::AudioStatus)> on_audio_status_changed_;
  std::function<void(unsigned int, ZOOMSDK::VideoStatus)> on_video_status_changed_;
  std::function<void(unsigned int, bool)> on_share_status_changed_;
  std::function<void(const UserStatusEvent&)> on_status_event_;
};
