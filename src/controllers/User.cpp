#include "controllers/User.h"

#include <algorithm>
#include <utility>

#if defined(WIN32)
#include <codecvt>
#include <locale>
#endif

#include "controllers/Media.h"
#include "events/MeetingAudioEvent.h"
#include "events/MeetingParticipantsEvent.h"
#include "events/MeetingShareEvent.h"
#include "events/MeetingVideoEvent.h"
#include "meeting_service_components/meeting_sharing_interface.h"
#include "meeting_service_interface.h"
#include "util/Checks.h"
#include "util/Logger.h"
#include "util/TimelineClock.h"

using namespace ZOOMSDK;

namespace {

std::string ToUtf8(const zchar_t* value) {
#if defined(WIN32)
  if (!value) {
    return {};
  }
  std::wstring wide(value);
  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
  return converter.to_bytes(wide);
#else
  return value ? std::string(value) : std::string();
#endif
}

bool IsAudioOn(AudioStatus status) {
  return status == Audio_UnMuted || status == Audio_UnMuted_ByHost;
}

bool IsVideoOn(VideoStatus status) { return status == Video_ON; }

}  // namespace

UserController::UserController(IMeetingService* meeting_service)
    : meeting_service_(meeting_service),
      participants_ctrl_(nullptr),
      audio_ctrl_(nullptr),
      video_ctrl_(nullptr),
      share_ctrl_(nullptr),
      media_controller_(nullptr) {
  ASSERT_NOT_NULL(meeting_service_);

  participants_ctrl_ = meeting_service_->GetMeetingParticipantsController();
  ASSERT_NOT_NULL(participants_ctrl_);
  participants_event_ = std::make_unique<MeetingParticipantsEvent>(*this);
  participants_ctrl_->SetEvent(participants_event_.get());

  audio_ctrl_ = meeting_service_->GetMeetingAudioController();
  ASSERT_NOT_NULL(audio_ctrl_);
  audio_event_ = std::make_unique<MeetingAudioEvent>(*this);
  audio_ctrl_->SetEvent(audio_event_.get());

  video_ctrl_ = meeting_service_->GetMeetingVideoController();
  ASSERT_NOT_NULL(video_ctrl_);
  video_event_ = std::make_unique<MeetingVideoEvent>(*this);
  video_ctrl_->SetEvent(video_event_.get());

  share_ctrl_ = meeting_service_->GetMeetingShareController();
  ASSERT_NOT_NULL(share_ctrl_);
  share_event_ = std::make_unique<MeetingShareEvent>(*this);
  share_ctrl_->SetEvent(share_event_.get());
}

UserController::~UserController() noexcept {
  if (participants_ctrl_) {
    participants_ctrl_->SetEvent(nullptr);
  }
  if (audio_ctrl_) {
    audio_ctrl_->SetEvent(nullptr);
  }
  if (video_ctrl_) {
    video_ctrl_->SetEvent(nullptr);
  }
  if (share_ctrl_) {
    share_ctrl_->SetEvent(nullptr);
  }
}

std::vector<ZOOMSDK::ZoomSDKSharingSourceInfo> UserController::GetActiveShareSources() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_share_sources_;
}

std::vector<UserSnapshot> UserController::GetUsers() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<UserSnapshot> snapshots;
  snapshots.reserve(users_.size());
  for (const auto& entry : users_) {
    snapshots.push_back(entry.second);
  }
  return snapshots;
}

std::optional<UserSnapshot> UserController::GetUser(unsigned int user_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = users_.find(user_id);
  if (it == users_.end()) {
    return std::nullopt;
  }
  return it->second;
}

bool UserController::UpdateUser(unsigned int user_id,
                                const std::function<bool(UserSnapshot&)>& mutator) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = users_.find(user_id);
  if (it == users_.end()) {
    return false;
  }
  return mutator(it->second);
}

void UserController::SetOnUserJoin(const std::function<void(unsigned int)>& cb) {
  on_user_join_ = cb;
}

void UserController::SetOnUserLeave(const std::function<void(unsigned int)>& cb) {
  on_user_leave_ = cb;
}

void UserController::SetOnAudioStatusChanged(
    const std::function<void(unsigned int, AudioStatus)>& cb) {
  on_audio_status_changed_ = cb;
}

void UserController::SetOnVideoStatusChanged(
    const std::function<void(unsigned int, VideoStatus)>& cb) {
  on_video_status_changed_ = cb;
}

void UserController::SetOnShareStatusChanged(const std::function<void(unsigned int, bool)>& cb) {
  on_share_status_changed_ = cb;
}

void UserController::SetOnStatusEvent(const std::function<void(const UserStatusEvent&)>& cb) {
  on_status_event_ = cb;
}

void UserController::InitializeState() {
  if (auto* self_user = participants_ctrl_->GetMySelfUser()) {
    self_user_id_ = self_user->GetUserID();
  }
  if (auto* list = participants_ctrl_->GetParticipantsList()) {
    for (int i = 0; i < list->GetCount(); ++i) {
      const unsigned int kUserId = list->GetItem(i);
      if (kUserId == self_user_id_) {
        continue;
      }
      if (auto* user_info = participants_ctrl_->GetUserByUserID(kUserId)) {
        auto snapshot = MakeSnapshot(user_info);
        {
          std::lock_guard<std::mutex> lock(mutex_);
          users_[kUserId] = snapshot;
        }
        EmitStatusEvent(UserStatusEvent::Type::kSnapshot, snapshot);
        
        // Notify MediaController of initial audio status to create streams for already-unmuted users
        if (snapshot.audio_on && media_controller_) {
          media_controller_->UpdateAudioStatus(kUserId, ZOOMSDK::Audio_UnMuted);
        }
      }
    }
  }
}



void UserController::HandleShareStatus(unsigned int user_id, const ZoomSDKSharingSourceInfo& info, bool is_starting) {
  EnsureUserCached(user_id);

  // Update active share sources list
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(active_share_sources_.begin(), active_share_sources_.end(),
                           [&info](const ZoomSDKSharingSourceInfo& s) {
                             return s.shareSourceID == info.shareSourceID;
                           });
    
    if (is_starting) {
      // Remove if exists (to update), then add
      if (it != active_share_sources_.end()) {
        active_share_sources_.erase(it);
      }
      active_share_sources_.push_back(info);
    } else {
      // Remove from list when ending
      if (it != active_share_sources_.end()) {
        active_share_sources_.erase(it);
      } else {
        // Share source not found, nothing to do
        return;
      }
    }
  }

  // Update user snapshot
  bool updated = UpdateUser(user_id, [is_starting](UserSnapshot& snapshot) {
    return std::exchange(snapshot.sharing, is_starting) != is_starting;
  });

  if (updated) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (is_starting) {
        sharing_users_.insert(user_id);
      } else {
        sharing_users_.erase(user_id);
      }
    }

    if (media_controller_) {
      media_controller_->UpdateShareSources(GetActiveShareSources());
    }

    if (auto callback = on_share_status_changed_) {
      callback(user_id, is_starting);
    }

    EmitStatusEvent(
        is_starting ? UserStatusEvent::Type::kShareStarted : UserStatusEvent::Type::kShareStopped,
        GetUser(user_id).value(), std::nullopt, std::nullopt, is_starting);
  }
}

void UserController::HandleParticipantJoin(unsigned int user_id) {
  if (user_id == self_user_id_) {
    return;
  }

  if (auto* user_info = participants_ctrl_->GetUserByUserID(user_id)) {
    auto snapshot = MakeSnapshot(user_info);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto sharing_it = sharing_users_.find(user_id);
      snapshot.sharing = sharing_it != sharing_users_.end();
      users_[user_id] = snapshot;
    }
    EmitStatusEvent(UserStatusEvent::Type::kJoined, snapshot);
    if (auto callback = on_user_join_) {
      callback(user_id);
    }
  }
}

void UserController::HandleParticipantLeft(unsigned int user_id) {
  bool had_user = false;
  std::optional<UserSnapshot> snapshot;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = users_.find(user_id);
    if (it != users_.end()) {
      snapshot = it->second;
      users_.erase(it);
      had_user = true;
    }
    sharing_users_.erase(user_id);
  }

  if (snapshot) {
    EmitStatusEvent(UserStatusEvent::Type::kLeft, *snapshot);
  }

  if (had_user) {
    if (auto callback = on_user_leave_) {
      callback(user_id);
    }
  }
}

void UserController::HandleAudioStatus(unsigned int user_id, AudioStatus status) {
  EnsureUserCached(user_id);

  const bool kAudioOn = IsAudioOn(status);
  bool updated = UpdateUser(user_id, [kAudioOn](UserSnapshot& snapshot) {
    return std::exchange(snapshot.audio_on, kAudioOn) != kAudioOn;
  });

  if (updated) {
    if (media_controller_) {
      media_controller_->UpdateAudioStatus(user_id, status);
    }
    
    if (auto callback = on_audio_status_changed_) {
      callback(user_id, status);
    }
    EmitStatusEvent(
        kAudioOn ? UserStatusEvent::Type::kAudioUnmuted : UserStatusEvent::Type::kAudioMuted,
        GetUser(user_id).value(), status);
  }
}

void UserController::HandleVideoStatus(unsigned int user_id, VideoStatus status) {
  EnsureUserCached(user_id);

  const bool kVideoOn = IsVideoOn(status);

  bool updated = UpdateUser(user_id, [kVideoOn](UserSnapshot& snapshot) {
    return std::exchange(snapshot.video_on, kVideoOn) != kVideoOn;
  });

  if (updated) {
    if (media_controller_) {
      media_controller_->UpdateCameraStatus(user_id, kVideoOn);
    }

    if (auto callback = on_video_status_changed_) {
      callback(user_id, status);
    }

    EmitStatusEvent(kVideoOn ? UserStatusEvent::Type::kVideoOn : UserStatusEvent::Type::kVideoOff,
                    GetUser(user_id).value(), std::nullopt, status);
  }
}


void UserController::EnsureUserCached(unsigned int user_id) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (users_.find(user_id) != users_.end()) {
      return;
    }
  }

  if (auto* user_info = participants_ctrl_->GetUserByUserID(user_id)) {
    auto snapshot = MakeSnapshot(user_info);
    std::lock_guard<std::mutex> lock(mutex_);
    auto sharing_it = sharing_users_.find(user_id);
    snapshot.sharing = sharing_it != sharing_users_.end();
    users_[user_id] = snapshot;
  }
}

UserSnapshot UserController::MakeSnapshot(IUserInfo* user_info) const {
  UserSnapshot snapshot{};
  snapshot.id = user_info->GetUserID();
  snapshot.name = ToUtf8(user_info->GetUserName());
  snapshot.avatar = ToUtf8(user_info->GetAvatarPath());
  snapshot.audio_on = !user_info->IsAudioMuted();
  snapshot.video_on = user_info->IsVideoOn();
  snapshot.sharing = false;
  return snapshot;
}

void UserController::EmitStatusEvent(UserStatusEvent::Type type, const UserSnapshot& snapshot,
                                     std::optional<ZOOMSDK::AudioStatus> audio,
                                     std::optional<ZOOMSDK::VideoStatus> video,
                                     std::optional<bool> share) {
  UserStatusEvent event{type, snapshot, audio, video, share, 0, 0};

  // Always capture absolute wall time
  event.wall_ts_ms = TimelineClock::SystemUnixMs();

  // Always compute media timeline timestamp (can be negative for pre-recording events)
  // This uses UnixToMediaSignedMs which returns 0 if timeline not set, 
  // or signed offset (can be negative) once timeline is established
  if (media_controller_) {
    event.media_ts_ms = media_controller_->UnixToMediaSignedMs(event.wall_ts_ms);
  }

  if (auto callback = on_status_event_) {
    callback(event);
  }
}

void UserController::HandleSpeakingStatus(unsigned int user_id, bool is_speaking) {
  if (user_id == 0) {
    return;
  }

  auto user_snapshot = GetUser(user_id);
  if (user_snapshot) {
    auto event_type = is_speaking ? UserStatusEvent::Type::kActiveSpeaking
                                  : UserStatusEvent::Type::kInactiveSpeaking;
    EmitStatusEvent(event_type, *user_snapshot);
  }
}
