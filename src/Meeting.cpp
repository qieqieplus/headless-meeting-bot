#include "Meeting.h"

#include "controllers/User.h"
#include "util/Checks.h"
#include "util/Logger.h"

using namespace ZOOMSDK;

Meeting::Meeting(const MeetingConfig& config, IMeetingService* meeting_service,
                 ISettingService* setting_service)
    : config_(config),
      meeting_service_(meeting_service),
      setting_service_(setting_service),
      is_joined_(false),
      media_controller_(nullptr),
      user_controller_(nullptr),
      reminder_event_(nullptr),
      meeting_service_event_(nullptr) {
  if (!meeting_service_ || !setting_service_) {
    Logger::GetInstance().Error("Services must be provided to create a Meeting");
    return;
  }

  media_controller_ = std::make_unique<MediaController>();
  user_controller_ = std::make_unique<UserController>(meeting_service_);

  ASSERT_NOT_NULL(media_controller_.get());
  ASSERT_NOT_NULL(user_controller_.get());

  // Connect UserController to MediaController for timeline clock access
  user_controller_->SetMediaController(media_controller_.get());

  SetupMeetingEvents();
}

Meeting::Meeting(Meeting&& other) noexcept
    : config_(std::move(other.config_)),
      meeting_service_(other.meeting_service_),
      setting_service_(other.setting_service_),
      is_joined_(other.is_joined_),
      media_controller_(std::move(other.media_controller_)),
      user_controller_(std::move(other.user_controller_)),
      reminder_event_(std::move(other.reminder_event_)),
      meeting_service_event_(std::move(other.meeting_service_event_)) {
  // Reset the moved-from object to a valid but empty state
  other.is_joined_ = false;
  other.meeting_service_ = nullptr;
  other.setting_service_ = nullptr;
}

Meeting& Meeting::operator=(Meeting&& other) noexcept {
  if (this != &other) {
    // Clean up current resources
    if (is_joined_) {
      Leave();
    }

    // Move assign all members (in declaration order)
    config_ = std::move(other.config_);
    meeting_service_ = other.meeting_service_;
    setting_service_ = other.setting_service_;
    is_joined_ = other.is_joined_;
    media_controller_ = std::move(other.media_controller_);
    user_controller_ = std::move(other.user_controller_);
    reminder_event_ = std::move(other.reminder_event_);
    meeting_service_event_ = std::move(other.meeting_service_event_);

    // Reset the moved-from object
    other.is_joined_ = false;
    other.meeting_service_ = nullptr;
    other.setting_service_ = nullptr;
  }
  return *this;
}

Meeting::~Meeting() noexcept {
  if (is_joined_) {
    Leave();
  }
  // Unset events before destruction to avoid dangling pointers in SDK
  if (!meeting_service_) {
    return;
  }
  if (meeting_service_event_) {
    meeting_service_->SetEvent(nullptr);
  }

  auto* reminder_controller = meeting_service_->GetMeetingReminderController();
  if (reminder_controller && reminder_event_) {
    reminder_controller->SetEvent(nullptr);
  }
}

SDKError Meeting::SetupMeetingEvents() {
  std::function<void()> on_join = [this]() {
    is_joined_ = true;
    user_controller_->InitializeState();
    MuteMyself();

    auto* reminder_controller = meeting_service_->GetMeetingReminderController();
    if (reminder_controller) {
      reminder_event_ = std::make_unique<MeetingReminderEvent>();
      reminder_controller->SetEvent(reminder_event_.get());
    }

    if (config_.UseRawVideo()) {
      user_controller_->SetOnVideoStatusChanged([this](unsigned int user_id, VideoStatus status) {
        media_controller_->UpdateCameraStatus(user_id, status == Video_ON);
      });
      // Prime MediaController with current shares and cameras
      media_controller_->UpdateShareSources(user_controller_->GetActiveShareSources());
      for (const auto& user : user_controller_->GetUsers()) {
        if (user.video_on) {
          media_controller_->UpdateCameraStatus(user.id, true);
        }
      }
    } else {
      user_controller_->SetOnVideoStatusChanged(nullptr);
    }

    if (config_.UseRawRecording()) {
      media_controller_->SetupRecording(meeting_service_->GetMeetingRecordingController(),
                                        config_.UseRawAudio(), config_.UseRawVideo());
    }
  };

  std::function<void()> on_leave = [this]() {
    is_joined_ = false;
    if (media_controller_) {
      media_controller_->CleanupRecording();
    }
    user_controller_.reset();
    Logger::GetInstance().Info("Left meeting");
  };

  meeting_service_event_ = std::make_unique<MeetingServiceEvent>(on_join, on_leave);

  return meeting_service_->SetEvent(meeting_service_event_.get());
}

SDKError Meeting::Join() {
  ASSERT_NOT_NULL(meeting_service_);

  auto id = config_.MeetingId();
  auto password = config_.Password();
  auto display_name = config_.DisplayName();

  if (id.empty() || password.empty()) {
    Logger::GetInstance().Error("You must provide an id and password to join a meeting");
    return SDKERR_INVALID_PARAMETER;
  }

  auto meeting_number = stoull(id);
  auto user_name = display_name.c_str();
  auto psw = password.c_str();

  JoinParam join_param;
  join_param.userType = ZOOM_SDK_NAMESPACE::SDK_UT_WITHOUT_LOGIN;

  JoinParam4WithoutLogin& param = join_param.param.withoutloginuserJoin;

  param.meetingNumber = meeting_number;
  param.userName = user_name;
  param.psw = psw;
  param.vanityID = nullptr;
  param.customer_key = nullptr;
  param.webinarToken = nullptr;
  param.isVideoOff = true;
  param.isAudioOff = false;

  if (!config_.JoinToken().empty()) {
    param.app_privilege_token = config_.JoinToken().c_str();
  }

  EnableAudio();

  return meeting_service_->Join(join_param);
}

SDKError Meeting::Start() {
  ASSERT_NOT_NULL(meeting_service_);

  StartParam start_param;
  start_param.userType = SDK_UT_NORMALUSER;

  StartParam4NormalUser normal_user;
  normal_user.vanityID = nullptr;
  normal_user.customer_key = nullptr;
  normal_user.isVideoOff = true;
  normal_user.isAudioOff = false;
  start_param.param.normaluserStart = normal_user;

  ZOOM_ERR_CHECK(meeting_service_->Start(start_param), "start meeting");
  return SDKERR_SUCCESS;
}

SDKError Meeting::Leave() {
  if (!is_joined_) {
    return SDKERR_SUCCESS;
  }

  ASSERT_NOT_NULL(meeting_service_);
  return meeting_service_->Leave(LEAVE_MEETING);
}

SDKError Meeting::StartOrJoin() {
  // should always join
  return IsMeetingStart() ? Start() : Join();
}

bool Meeting::IsMeetingStart() const { return config_.IsMeetingStart(); }

std::unique_ptr<Meeting> Meeting::CreateMeeting(const MeetingConfig& meeting_config,
                                                IMeetingService* meeting_service,
                                                ISettingService* setting_service) {
  if (!meeting_service || !setting_service) {
    Logger::GetInstance().Error(
        "MeetingService and SettingService must "
        "be provided to create a Meeting");
    return nullptr;
  }

  auto meeting = std::make_unique<Meeting>(meeting_config, meeting_service, setting_service);

  // Check if the object was properly constructed
  if (!meeting->meeting_service_ || !meeting->setting_service_ || !meeting->media_controller_ ||
      !meeting->user_controller_) {
    Logger::GetInstance().Error("Failed to create Meeting object - invalid state");
    return nullptr;
  }

  return meeting;
}

std::unique_ptr<Meeting> Meeting::CreateMeeting(
    const std::string& meeting_id, const std::string& password, const std::string& display_name,
    bool is_meeting_start, const std::string& join_token, bool use_raw_audio, bool use_raw_video,
    IMeetingService* meeting_service, ISettingService* setting_service) {
  MeetingConfig config(meeting_id, password, display_name, is_meeting_start, join_token,
                       use_raw_audio, use_raw_video);
  return CreateMeeting(config, meeting_service, setting_service);
}

void Meeting::SetOnMeetingStatusChanged(
    const std::function<void(ZOOMSDK::MeetingStatus, int)>& cb) {
  if (meeting_service_event_) {
    meeting_service_event_->SetOnStatusChanged(cb);
  }
}

// Allow incoming audio while keep the bot muted
void Meeting::MuteMyself() {
  auto* participants_ctrl = meeting_service_->GetMeetingParticipantsController();
  auto* audio_ctrl = meeting_service_->GetMeetingAudioController();
  auto* video_ctrl = meeting_service_->GetMeetingVideoController();

  ASSERT_NOT_NULL(participants_ctrl);
  ASSERT_NOT_NULL(audio_ctrl);
  ASSERT_NOT_NULL(video_ctrl);

  unsigned int bot_user_id = user_controller_->GetBotUserId();
  if (bot_user_id != 0) {
    audio_ctrl->MuteAudio(bot_user_id);
    // a workaround to join audio
    // https://devforum.zoom.us/t/cant-record-audio-with-linux-meetingsdk-after-6-3-5-6495-error-code-32/130689/10
    audio_ctrl->JoinVoip();
    video_ctrl->MuteVideo();
  }
}

void Meeting::EnableAudio() const {
  if (!config_.UseRawAudio()) {
    return;
  }

  auto* audio_settings = setting_service_->GetAudioSettings();
  ASSERT_NOT_NULL(audio_settings);
  audio_settings->EnableAutoJoinAudio(true);
}
