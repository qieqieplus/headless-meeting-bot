#include "UserController.h"

#include <algorithm>
#include <utility>

#if defined(WIN32)
#include <codecvt>
#include <locale>
#endif

#include "MediaController.h"
#include "events/MeetingAudioEvent.h"
#include "events/MeetingParticipantsEvent.h"
#include "events/MeetingShareEvent.h"
#include "events/MeetingVideoEvent.h"
#include "meeting_service_components/meeting_sharing_interface.h"
#include "meeting_service_interface.h"
#include "util/Checks.h"
#include "util/Logger.h"

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
  auto share_start_handler = [this](const ZoomSDKSharingSourceInfo& info) {
    this->OnShareStart(info);
  };
  auto share_end_handler = [this](const ZoomSDKSharingSourceInfo& info) { this->OnShareEnd(info); };
  share_event_ = std::make_unique<MeetingShareEvent>(share_start_handler, share_end_handler);
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
  // Refresh participants and their snapshots
  PopulateInitialUsers();
}

void UserController::OnShareStart(const ZoomSDKSharingSourceInfo& info) {
  HandleShareStatus(info.userid, true);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(active_share_sources_.begin(), active_share_sources_.end(),
                           [&info](const ZoomSDKSharingSourceInfo& s) {
                             return s.shareSourceID == info.shareSourceID;
                           });
    if (it != active_share_sources_.end()) {
      active_share_sources_.erase(it);
    }
    active_share_sources_.push_back(info);
  }

  if (media_controller_) {
    media_controller_->UpdateShareSources(GetActiveShareSources());
  }
}

void UserController::OnShareEnd(const ZoomSDKSharingSourceInfo& info) {
  HandleShareStatus(info.userid, false);

  bool removed = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(active_share_sources_.begin(), active_share_sources_.end(),
                           [&info](const ZoomSDKSharingSourceInfo& s) {
                             return s.shareSourceID == info.shareSourceID;
                           });
    if (it != active_share_sources_.end()) {
      active_share_sources_.erase(it);
      removed = true;
    }
  }

  if (removed && media_controller_) {
    media_controller_->UpdateShareSources(GetActiveShareSources());
  }
}

void UserController::HandleParticipantJoin(unsigned int user_id) {
  // Skip the bot itself
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
    if (auto callback = on_video_status_changed_) {
      callback(user_id, status);
    }

    EmitStatusEvent(kVideoOn ? UserStatusEvent::Type::kVideoOn : UserStatusEvent::Type::kVideoOff,
                    GetUser(user_id).value(), std::nullopt, status);
  }
}

void UserController::HandleShareStatus(unsigned int user_id, bool is_sharing) {
  EnsureUserCached(user_id);

  bool updated = UpdateUser(user_id, [is_sharing](UserSnapshot& snapshot) {
    return std::exchange(snapshot.sharing, is_sharing) != is_sharing;
  });

  if (updated) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (is_sharing) {
        sharing_users_.insert(user_id);
      } else {
        sharing_users_.erase(user_id);
      }
    }

    if (auto callback = on_share_status_changed_) {
      callback(user_id, is_sharing);
    }

    EmitStatusEvent(
        is_sharing ? UserStatusEvent::Type::kShareStarted : UserStatusEvent::Type::kShareStopped,
        GetUser(user_id).value(), std::nullopt, std::nullopt, is_sharing);
  }
}

void UserController::PopulateInitialUsers() {
  if (auto* self_user = participants_ctrl_->GetMySelfUser()) {
    self_user_id_ = self_user->GetUserID();
  }
  if (auto* list = participants_ctrl_->GetParticipantsList()) {
    for (int i = 0; i < list->GetCount(); ++i) {
      const unsigned int kUserId = list->GetItem(i);
      // Skip the bot itself
      if (kUserId == self_user_id_) {
        continue;
      }
      if (auto* user_info = participants_ctrl_->GetUserByUserID(kUserId)) {
        auto snapshot = MakeSnapshot(user_info);
        std::lock_guard<std::mutex> lock(mutex_);
        users_[kUserId] = snapshot;
        EmitStatusEvent(UserStatusEvent::Type::kSnapshot, snapshot);
      }
    }
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
  UserStatusEvent event{type, snapshot, audio, video, share, 0};

  // Populate timestamp from MediaController's timeline clock if available
  if (media_controller_ && media_controller_->TimelineReady()) {
    event.timestamp_ms = media_controller_->Now();
  }

  if (auto callback = on_status_event_) {
    callback(event);
  }
}
