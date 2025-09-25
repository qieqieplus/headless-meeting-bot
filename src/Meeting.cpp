#include "Meeting.h"
#include "util/Logger.h"
#include <sstream>

Meeting::Meeting(const MeetingConfig& config, IMeetingService* meetingService, ISettingService* settingService)
    : m_config(config)
    , m_videoHelper(nullptr)
    , m_videoSource(nullptr)
    , m_audioHelper(nullptr)
    , m_audioSource(nullptr)
    , m_isJoined(false)
    , m_isRecording(false)
    , m_meetingService(meetingService)
    , m_settingService(settingService) {

    if (!m_meetingService || !m_settingService) {
        Util::Logger::getInstance().error("MeetingService and SettingService must be provided to create a Meeting");
        return;
    }

    setupMeetingEvents();
}

Meeting::~Meeting() {
    if (m_isJoined) {
        leave();
    }
    
    if (m_audioHelper && m_audioSource) {
        m_audioHelper->unSubscribe();
    }
    
    // Video helper cleanup disabled for simplified C API
    /*
    if (m_videoHelper) {
        m_videoHelper->unSubscribe();
        destroyRenderer(m_videoHelper);
        m_videoHelper = nullptr;
    }
    */
    
    // Meeting service is now managed at SDK level
    
    // delete m_audioSource;
    // delete m_videoSource; // Disabled for simplified C API
}


SDKError Meeting::setupMeetingEvents() {
    function<void()> onJoin = [&]() {
        m_isJoined = true;
        Util::Logger::getInstance().success("Joined meeting successfully");

        auto* reminderController = m_meetingService->GetMeetingReminderController();
        reminderController->SetEvent(new MeetingReminderEvent());

        if (m_config.useRawRecording()) {
            auto recordingCtrl = m_meetingService->GetMeetingRecordingController();

            function<void(bool)> onRecordingPrivilegeChanged = [&](bool canRec) {
                if (canRec)
                    startRawRecording();
                else
                    stopRawRecording();
            };

            auto recordingEvent = new MeetingRecordingCtrlEvent(onRecordingPrivilegeChanged);
            recordingCtrl->SetEvent(recordingEvent);

            auto e = recordingCtrl->CanStartRawRecording();
            string action = " local recording privilege";

            if (e == SDKERR_SUCCESS) {
                e = startRawRecording();
                action = "has" + action;
            } else {
                e = recordingCtrl->RequestLocalRecordingPrivilege();
                action = "request" + action;
            }

            hasError(e, action);
        }
    };
    
    function<void()> onLeave = [&]() {
        m_isJoined = false;
        m_isRecording = false;
        Util::Logger::getInstance().info("Left meeting");
    };

    auto meetingServiceEvent = new MeetingServiceEvent(onJoin, onLeave);

    return m_meetingService->SetEvent(meetingServiceEvent);
}

SDKError Meeting::join() {
    if (!m_meetingService) {
        return SDKERR_UNINITIALIZE;
    }
    
    auto id = m_config.meetingId();
    auto password = m_config.password();
    auto displayName = m_config.displayName();

    if (id.empty() || password.empty()) {
        cerr << "you must provide an id and password to join a meeting" << endl;
        return SDKERR_UNINITIALIZE;
    }

    auto meetingNumber = stoull(id);
    auto userName = displayName.c_str();
    auto psw = password.c_str();

    JoinParam joinParam;
    joinParam.userType = ZOOM_SDK_NAMESPACE::SDK_UT_WITHOUT_LOGIN;

    JoinParam4WithoutLogin& param = joinParam.param.withoutloginuserJoin;

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
        auto* audioSettings = m_settingService->GetAudioSettings();
        if (!audioSettings) return SDKERR_INTERNAL_ERROR;

        audioSettings->EnableAutoJoinAudio(true);
    }

    return m_meetingService->Join(joinParam);
}

SDKError Meeting::start() {
    if (!m_meetingService) {
        return SDKERR_UNINITIALIZE;
    }
    
    StartParam startParam;
    startParam.userType = SDK_UT_NORMALUSER;

    StartParam4NormalUser normalUser;
    normalUser.vanityID = nullptr;
    normalUser.customer_key = nullptr;
    normalUser.isVideoOff = true;
    normalUser.isAudioOff = false;

    SDKError err = m_meetingService->Start(startParam);
    hasError(err, "start meeting");

    return err;
}

SDKError Meeting::leave() {
    if (!m_meetingService)
        return SDKERR_UNINITIALIZE;

    auto status = m_meetingService->GetMeetingStatus();
    if (status == MEETING_STATUS_IDLE)
        return SDKERR_WRONG_USAGE;

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
    if (!m_meetingService || m_isRecording) {
        return SDKERR_UNINITIALIZE;
    }

    SDKError err;

    auto recCtrl = m_meetingService->GetMeetingRecordingController();
    if (recCtrl->CanStartRawRecording() != SDKERR_SUCCESS)
        return SDKERR_UNAUTHENTICATION;

    err = recCtrl->StartRawRecording();
    if (hasError(err, "start raw recording"))
        return err;

    // Video recording disabled in simplified C API for now
    /*
    if (m_config.useRawVideo()) {
        if (!m_videoSource)
            m_videoSource = new ZoomSDKRendererDelegate();
            
        err = createRenderer(&m_videoHelper, m_videoSource);
        if (hasError(err, "create raw video renderer"))
            return err;

        auto participantCtl = meetingService->GetMeetingParticipantsController();
        int uid = participantCtl->GetParticipantsList()->GetItem(0);

        m_videoHelper->setRawDataResolution(ZoomSDKResolution_720P);
        err = m_videoHelper->subscribe(uid, RAW_DATA_TYPE_VIDEO);
        if (hasError(err, "subscribe to raw video"))
            return err;
    }
    */

    if (m_config.useRawAudio()) {
        m_audioHelper = GetAudioRawdataHelper();
        if (!m_audioHelper)
            return SDKERR_UNINITIALIZE;

        err = m_audioHelper->subscribe(m_audioSource);
        if (hasError(err, "subscribe to raw audio"))
            return err;
    }
    
    m_isRecording = true;
    return SDKERR_SUCCESS;
}

SDKError Meeting::stopRawRecording() {
    if (!m_meetingService || !m_isRecording) {
        return SDKERR_UNINITIALIZE;
    }

    auto recCtrl = m_meetingService->GetMeetingRecordingController();
    auto err = recCtrl->StopRawRecording();
    hasError(err, "stop raw recording");
    
    if (m_audioHelper && m_audioSource) {
        m_audioHelper->unSubscribe();
    }
    
    // Video helper cleanup disabled for simplified C API
    /*
    if (m_videoHelper) {
        m_videoHelper->unSubscribe();
        destroyRenderer(m_videoHelper);
        m_videoHelper = nullptr;
    }
    */
    
    m_isRecording = false;
    return err;
}

bool Meeting::isMeetingStart() const {
    return m_config.isMeetingStart();
}

Meeting* Meeting::createMeeting(const MeetingConfig& meetingConfig, IMeetingService* meetingService, ISettingService* settingService) {
    if (!meetingService || !settingService) {
        Util::Logger::getInstance().error("MeetingService and SettingService must be provided to create a Meeting");
        return nullptr;
    }

    return new Meeting(meetingConfig, meetingService, settingService);
}

Meeting* Meeting::createMeeting(const string& meetingId,
                               const string& password,
                               const string& displayName,
                               bool isMeetingStart,
                               const string& joinToken,
                               bool useRawAudio,
                               bool useRawVideo,
                               IMeetingService* meetingService,
                               ISettingService* settingService) {
    MeetingConfig config(meetingId, password, displayName, isMeetingStart, joinToken, useRawAudio, useRawVideo);
    return createMeeting(config, meetingService, settingService);
}

bool Meeting::hasError(const SDKError e, const string& action) {
    auto isError = e != SDKERR_SUCCESS;

    if(!action.empty()) {
        if (isError) {
            stringstream ss;
            ss << "failed to " << action << " with status " << e;
            Util::Logger::getInstance().error(ss.str());
        } else {
            Util::Logger::getInstance().success(action);
        }
    }
    return isError;
}
