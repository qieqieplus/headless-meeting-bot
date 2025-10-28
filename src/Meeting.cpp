#include "Meeting.h"
#include "util/Checks.h"
#include "util/Logger.h"

using namespace ZOOMSDK;

Meeting::Meeting(const MeetingConfig &config, IMeetingService *meetingService,
                 ISettingService *settingService)
    : m_config(config), m_isJoined(false), m_isRecording(false),
      m_meetingService(meetingService), m_settingService(settingService),
      m_mediaController(std::make_unique<MediaController>()) {

  if (!m_meetingService || !m_settingService) {
    Logger::getInstance().error(
        "Services must be provided to create a Meeting");
    return;
  }

  setupMeetingEvents();
}

Meeting::~Meeting() {
  if (m_isJoined) {
    leave();
  }
  // Unset events before destruction to avoid dangling pointers in SDK
  if (!m_meetingService) {
    return;
  }
  if (m_meetingServiceEvent) {
    m_meetingService->SetEvent(nullptr);
  }

  auto *reminderController = m_meetingService->GetMeetingReminderController();
  if (reminderController && m_reminderEvent) {
    reminderController->SetEvent(nullptr);
  }

  auto *recordingCtrl = m_meetingService->GetMeetingRecordingController();
  if (recordingCtrl && m_recordingEvent) {
    recordingCtrl->SetEvent(nullptr);
  }

  auto *shareCtrl = m_meetingService->GetMeetingShareController();
  if (shareCtrl && m_shareEvent) {
    shareCtrl->SetEvent(nullptr);
  }
}

SDKError Meeting::setupMeetingEvents() {
  std::function<void()> onJoin = [this]() {
    m_isJoined = true;
    Logger::getInstance().success("Joined meeting successfully");

    // mute the bot video & audio by default
    auto *participantsCtrl =
        m_meetingService->GetMeetingParticipantsController();
    ASSERT_NOT_NULL(participantsCtrl);
    if (auto *botUser = participantsCtrl->GetMySelfUser()) {
      auto *audioCtrl = m_meetingService->GetMeetingAudioController();
      ASSERT_NOT_NULL(audioCtrl);
      audioCtrl->MuteAudio(botUser->GetUserID());
      // a workaround to join audio
      // https://devforum.zoom.us/t/cant-record-audio-with-linux-meetingsdk-after-6-3-5-6495-error-code-32/130689/10
      audioCtrl->JoinVoip();

      auto *videoCtrl = m_meetingService->GetMeetingVideoController();
      ASSERT_NOT_NULL(videoCtrl);
      videoCtrl->MuteVideo();
    }

    auto *reminderController = m_meetingService->GetMeetingReminderController();
    if (reminderController) {
      m_reminderEvent = std::make_unique<MeetingReminderEvent>();
      reminderController->SetEvent(m_reminderEvent.get());
    }

    // Setup share event if we're capturing video (which is always share)
    if (m_config.useRawVideo() && m_mediaController) {
      auto *shareCtrl = m_meetingService->GetMeetingShareController();
      ASSERT_NOT_NULL(shareCtrl);
      auto onShareStart = [this](const ZoomSDKSharingSourceInfo &info) {
        m_mediaController->onShareStart(info);
      };
      auto onShareEnd = [this](const ZoomSDKSharingSourceInfo &info) {
        m_mediaController->onShareEnd(info);
      };
      m_shareEvent =
          std::make_unique<MeetingShareEvent>(onShareStart, onShareEnd);
      shareCtrl->SetEvent(m_shareEvent.get());
    }

    if (m_config.useRawRecording()) {
      auto recordingCtrl = m_meetingService->GetMeetingRecordingController();
      ASSERT_NOT_NULL(recordingCtrl);

      std::function<void(bool)> onRecordingPrivilegeChanged =
          [this](bool canRec) {
            if (canRec)
              startRawRecording();
            else
              stopRawRecording();
          };

      m_recordingEvent = std::make_unique<MeetingRecordingCtrlEvent>(
          onRecordingPrivilegeChanged);
      recordingCtrl->SetEvent(m_recordingEvent.get());

      auto e = recordingCtrl->CanStartRawRecording();
      if (e == SDKERR_SUCCESS) {
        startRawRecording();
      } else {
        recordingCtrl->RequestLocalRecordingPrivilege();
      }
    }
  };

  std::function<void()> onLeave = [this]() {
    m_isJoined = false;
    m_isRecording = false;
    Logger::getInstance().info("Left meeting");
  };

  m_meetingServiceEvent =
      std::make_unique<MeetingServiceEvent>(onJoin, onLeave);

  return m_meetingService->SetEvent(m_meetingServiceEvent.get());
}

SDKError Meeting::join() {
  ASSERT_NOT_NULL(m_meetingService);

  auto id = m_config.meetingId();
  auto password = m_config.password();
  auto displayName = m_config.displayName();

  if (id.empty() || password.empty()) {
    Logger::getInstance().error(
        "You must provide an id and password to join a meeting");
    return SDKERR_INVALID_PARAMETER;
  }

  auto meetingNumber = stoull(id);
  auto userName = displayName.c_str();
  auto psw = password.c_str();

  JoinParam joinParam;
  joinParam.userType = ZOOM_SDK_NAMESPACE::SDK_UT_WITHOUT_LOGIN;

  JoinParam4WithoutLogin &param = joinParam.param.withoutloginuserJoin;

  param.meetingNumber = meetingNumber;
  param.userName = userName;
  param.psw = psw;
  param.vanityID = nullptr;
  param.customer_key = nullptr;
  param.webinarToken = nullptr;
  param.isVideoOff = true;
  param.isAudioOff = false;

  if (!m_config.joinToken().empty())
    param.app_privilege_token = m_config.joinToken().c_str();

  if (m_config.useRawAudio()) {
    auto *audioSettings = m_settingService->GetAudioSettings();
    ASSERT_NOT_NULL(audioSettings);
    audioSettings->EnableAutoJoinAudio(true);
  }

  return m_meetingService->Join(joinParam);
}

SDKError Meeting::start() {
  ASSERT_NOT_NULL(m_meetingService);

  StartParam startParam;
  startParam.userType = SDK_UT_NORMALUSER;

  StartParam4NormalUser normalUser;
  normalUser.vanityID = nullptr;
  normalUser.customer_key = nullptr;
  normalUser.isVideoOff = true;
  normalUser.isAudioOff = false;
  startParam.param.normaluserStart = normalUser;

  ZOOM_ERR_CHECK(m_meetingService->Start(startParam), "start meeting");
  return SDKERR_SUCCESS;
}

SDKError Meeting::leave() {
  ASSERT_NOT_NULL(m_meetingService);

  if (m_isRecording) {
    stopRawRecording();
  }

  return m_meetingService->Leave(LEAVE_MEETING);
}

SDKError Meeting::startOrJoin() {
  if (isMeetingStart())
    return start();
  else
    return join();
}

SDKError Meeting::startRawRecording() {
  ASSERT_NOT_NULL(m_meetingService);

  if (m_isRecording) {
    return SDKERR_SUCCESS;
  }

  auto recCtrl = m_meetingService->GetMeetingRecordingController();
  ASSERT_NOT_NULL(recCtrl);

  ZOOM_ERR_CHECK(recCtrl->StartRawRecording(), "start raw recording");

  // Delegate audio/video subscription to MediaController
  if (m_mediaController) {
    // Get current active shares to pass to MediaController
    std::vector<ZOOMSDK::ZoomSDKSharingSourceInfo> currentShares;
    auto *shareCtrl = m_meetingService->GetMeetingShareController();
    if (shareCtrl) {
      if (auto *sharers = shareCtrl->GetViewableSharingUserList()) {
        for (int i = 0; i < sharers->GetCount(); i++) {
          unsigned int userId = sharers->GetItem(i);
          auto *shareList = shareCtrl->GetSharingSourceInfoList(userId);
          if (shareList) {
            for (int j = 0; j < shareList->GetCount(); j++) {
              currentShares.push_back(shareList->GetItem(j));
            }
          }
        }
      }
    }

    auto mediaErr = m_mediaController->startMedia(
        m_config.useRawAudio(), m_config.useRawVideo(), currentShares);
    if (mediaErr != SDKERR_SUCCESS) {
      recCtrl->StopRawRecording();
      Logger::getInstance().error(
          "failed to start media recording with status " +
          std::to_string(mediaErr));
      return mediaErr;
    }
  }

  m_isRecording = true;
  return SDKERR_SUCCESS;
}

SDKError Meeting::stopRawRecording() {
  ASSERT_NOT_NULL(m_meetingService);
  if (!m_isRecording)
    return SDKERR_SUCCESS;

  m_isRecording = false;

  // Delegate audio/video cleanup to MediaController
  if (m_mediaController) {
    m_mediaController->stopMedia();
  }

  auto recCtrl = m_meetingService->GetMeetingRecordingController();
  ASSERT_NOT_NULL(recCtrl);

  return recCtrl->StopRawRecording();
}

bool Meeting::isMeetingStart() const { return m_config.isMeetingStart(); }

Meeting *Meeting::createMeeting(const MeetingConfig &meetingConfig,
                                IMeetingService *meetingService,
                                ISettingService *settingService) {
  if (!meetingService || !settingService) {
    Logger::getInstance().error("MeetingService and SettingService must "
                                "be provided to create a Meeting");
    return nullptr;
  }

  return new Meeting(meetingConfig, meetingService, settingService);
}

Meeting *Meeting::createMeeting(
    const std::string &meetingId, const std::string &password,
    const std::string &displayName, bool isMeetingStart,
    const std::string &joinToken, bool useRawAudio, bool useRawVideo,
    IMeetingService *meetingService, ISettingService *settingService) {
  MeetingConfig config(meetingId, password, displayName, isMeetingStart,
                       joinToken, useRawAudio, useRawVideo);
  return createMeeting(config, meetingService, settingService);
}

bool Meeting::hasError(const SDKError e, const std::string &action) {
  auto isError = e != SDKERR_SUCCESS;

  if (!action.empty()) {
    if (isError) {
      Logger::getInstance().error("failed to " + action + " with status " +
                                  std::to_string(e));
    } else {
      Logger::getInstance().success(action);
    }
  }
  return isError;
}
