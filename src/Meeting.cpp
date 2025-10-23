#include "Meeting.h"
#include "util/Logger.h"
#include <sstream>
#include <thread>
#include <chrono>
#include <algorithm>

using namespace ZOOMSDK;

Meeting::Meeting(const MeetingConfig& config, IMeetingService* meetingService, ISettingService* settingService)
    : m_config(config)
    , m_videoHelper(nullptr)
    , m_videoSource(nullptr)
    , m_audioHelper(nullptr)
    , m_audioSource(nullptr)
    , m_isJoined(false)
    , m_isRecording(false)
    , m_shareSubscribed(false)
    , m_meetingService(meetingService)
    , m_settingService(settingService) {

    if (!m_meetingService || !m_settingService) {
        Util::Logger::getInstance().error("Services must be provided to create a Meeting");
        return;
    }

    setupMeetingEvents();
}

Meeting::~Meeting() {
    if (m_audioHelper) {
        m_audioHelper->unSubscribe();
    }

    if (m_videoHelper) {
        m_videoHelper->unSubscribe();
        destroyRenderer(m_videoHelper);
        m_videoHelper = nullptr;
    }

    // Unset events before destruction to avoid dangling pointers in SDK
    if (m_meetingService && m_meetingServiceEvent) {
        m_meetingService->SetEvent(nullptr);
    }

    auto* reminderController = m_meetingService ? m_meetingService->GetMeetingReminderController() : nullptr;
    if (reminderController && m_reminderEvent) {
        reminderController->SetEvent(nullptr);
    }

    auto* recordingCtrl = m_meetingService ? m_meetingService->GetMeetingRecordingController() : nullptr;
    if (recordingCtrl && m_recordingEvent) {
        recordingCtrl->SetEvent(nullptr);
    }

    auto* shareCtrl = m_meetingService ? m_meetingService->GetMeetingShareController() : nullptr;
    if (shareCtrl && m_shareEvent) {
        shareCtrl->SetEvent(nullptr);
    }

    if (m_isJoined) {
        leave();
    }
}


SDKError Meeting::setupMeetingEvents() {
    std::function<void()> onJoin = [this]() {
        m_isJoined = true;
        Util::Logger::getInstance().success("Joined meeting successfully");

        // mute the bot video & audio by default
        auto* participantsCtrl = m_meetingService->GetMeetingParticipantsController();
        if (participantsCtrl) {
            if (auto* botUser = participantsCtrl->GetMySelfUser()) {
                
                if (auto* audioCtrl = m_meetingService->GetMeetingAudioController()) {
                    audioCtrl->MuteAudio(botUser->GetUserID());
                    // a workaround to join audio
                    // https://devforum.zoom.us/t/cant-record-audio-with-linux-meetingsdk-after-6-3-5-6495-error-code-32/130689/10
                    audioCtrl->JoinVoip();
                }

                if (auto* videoCtrl = m_meetingService->GetMeetingVideoController()) {
                    videoCtrl->MuteVideo();
                }
            }
        }
        

        auto* reminderController = m_meetingService->GetMeetingReminderController();
        if (reminderController) {
            m_reminderEvent = std::make_unique<MeetingReminderEvent>();
            reminderController->SetEvent(m_reminderEvent.get());
        }

        // Setup share event if we're capturing video (which is always share)
        if (m_config.useRawVideo()) {
            auto* shareCtrl = m_meetingService->GetMeetingShareController();
            if (shareCtrl) {
                auto onShareStart = [this](const ZoomSDKSharingSourceInfo& info) {
                    subscribeShare(info);
                };
                auto onShareEnd = [this](const ZoomSDKSharingSourceInfo& info) {
                    unSubscribeShare(info);
                };
                m_shareEvent = std::make_unique<MeetingShareEvent>(onShareStart, onShareEnd);
                shareCtrl->SetEvent(m_shareEvent.get());
            }
        }

        if (m_config.useRawRecording()) {
            auto recordingCtrl = m_meetingService->GetMeetingRecordingController();
            if (!recordingCtrl) {
                Util::Logger::getInstance().error("Recording controller not available");
                return;
            }

            std::function<void(bool)> onRecordingPrivilegeChanged = [this](bool canRec) {
                if (canRec)
                    startRawRecording();
                else
                    stopRawRecording();
            };

            m_recordingEvent = std::make_unique<MeetingRecordingCtrlEvent>(onRecordingPrivilegeChanged);
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
        Util::Logger::getInstance().info("Left meeting");
    };

    m_meetingServiceEvent = std::make_unique<MeetingServiceEvent>(onJoin, onLeave);

    return m_meetingService->SetEvent(m_meetingServiceEvent.get());
}

SDKError Meeting::join() {
    if (!m_meetingService) {
        return SDKERR_UNINITIALIZE;
    }
    
    auto id = m_config.meetingId();
    auto password = m_config.password();
    auto displayName = m_config.displayName();

    if (id.empty() || password.empty()) {
        Util::Logger::getInstance().error("you must provide an id and password to join a meeting");
        return SDKERR_INVALID_PARAMETER;
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
    startParam.param.normaluserStart = normalUser;

    SDKError err = m_meetingService->Start(startParam);
    hasError(err, "start meeting");

    return err;
}

SDKError Meeting::leave() {
    if (!m_meetingService)
        return SDKERR_UNINITIALIZE;

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
    if (!m_meetingService) {
        return SDKERR_UNINITIALIZE;
    }

    if (m_isRecording) {
        return SDKERR_SUCCESS;
    }

    SDKError err;

    auto recCtrl = m_meetingService->GetMeetingRecordingController();
    err = recCtrl->StartRawRecording();
    if (hasError(err, "start raw recording"))
        return err;

    if (m_config.useRawVideo()) {
        // Video always means shared screen content
        if (!m_videoSource) {
            Util::Logger::getInstance().error("Video source delegate not set");
            return SDKERR_UNINITIALIZE;
        }

        err = createRenderer(&m_videoHelper, m_videoSource);
        if (hasError(err, "create renderer"))
            return err;

        m_videoHelper->setRawDataResolution(ZoomSDKResolution_720P);

        // Check if there's already an active share to subscribe to
        if (auto* shareCtrl = m_meetingService->GetMeetingShareController()) {
            if (auto* sharers = shareCtrl->GetViewableSharingUserList()) {
                for (int i = 0; i < sharers->GetCount(); i++) {
                    unsigned int userId = sharers->GetItem(i);
                    auto* shareList = shareCtrl->GetSharingSourceInfoList(userId);
                    if (shareList) {
                        for (int j = 0; j < shareList->GetCount(); j++) {
                            subscribeShare(shareList->GetItem(j));
                        }
                    }
                }
            }
        }
    }

    if (m_config.useRawAudio() && m_audioSource) {
        m_audioHelper = GetAudioRawdataHelper();
        if (!m_audioHelper)
            return SDKERR_UNINITIALIZE;

        m_audioHelper->subscribe(m_audioSource);
        if (hasError(err, "subscribe to raw audio"))
            return err;
    }
    
    m_isRecording = true;
    return SDKERR_SUCCESS;
}

SDKError Meeting::stopRawRecording() {
    if (!m_meetingService) return SDKERR_UNINITIALIZE;
    if (!m_isRecording) return SDKERR_SUCCESS;

    auto recCtrl = m_meetingService->GetMeetingRecordingController();
    if (!recCtrl) return SDKERR_UNINITIALIZE;

    auto err = recCtrl->StopRawRecording();
    hasError(err, "stop raw recording");
    
    if (m_audioHelper) {
        m_audioHelper->unSubscribe();
    }
    
    if (m_videoHelper) {
        m_videoHelper->unSubscribe();
        destroyRenderer(m_videoHelper);
        m_videoHelper = nullptr;
    }
    m_shareSubscribed = false;
    m_shareSourceIds.clear();
    
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

Meeting* Meeting::createMeeting(const std::string& meetingId,
                               const std::string& password,
                               const std::string& displayName,
                               bool isMeetingStart,
                               const std::string& joinToken,
                               bool useRawAudio,
                               bool useRawVideo,
                               IMeetingService* meetingService,
                               ISettingService* settingService) {
    MeetingConfig config(meetingId, password, displayName, isMeetingStart, joinToken, useRawAudio, useRawVideo);
    return createMeeting(config, meetingService, settingService);
}

bool Meeting::hasError(const SDKError e, const std::string& action) {
    auto isError = e != SDKERR_SUCCESS;

    if(!action.empty()) {
        if (isError) {
            std::stringstream ss;
            ss << "failed to " << action << " with status " << e;
            Util::Logger::getInstance().error(ss.str());
        } else {
            Util::Logger::getInstance().success(action);
        }
    }
    return isError;
}

void Meeting::subscribeShare(const ZoomSDKSharingSourceInfo& shareInfo) {
    if (shareInfo.contentType != SHARE_TYPE_DATA) {
        return;
    }

    const unsigned int sourceId = shareInfo.shareSourceID;
    auto it = std::find(m_shareSourceIds.begin(), m_shareSourceIds.end(), sourceId);
    if (it != m_shareSourceIds.end()) {
        m_shareSourceIds.erase(it);
    }
    m_shareSourceIds.push_back(sourceId);

    subscribeTopShare();
}

void Meeting::unSubscribeShare(const ZoomSDKSharingSourceInfo& shareInfo) {
    const unsigned int sourceId = shareInfo.shareSourceID;
    const bool wasTop = !m_shareSourceIds.empty() && m_shareSourceIds.back() == sourceId;

    auto it = std::find(m_shareSourceIds.begin(), m_shareSourceIds.end(), sourceId);
    if (it == m_shareSourceIds.end()) {
        return; // Not tracked
    }
    m_shareSourceIds.erase(it);

    if (wasTop) {
        if (m_shareSubscribed && m_videoHelper) {
            m_videoHelper->unSubscribe();
            m_shareSubscribed = false;
            Util::Logger::getInstance().success("Unsubscribed from share source " + std::to_string(sourceId));
        }
        subscribeTopShare();
    }
}

void Meeting::subscribeTopShare() {
    if (!m_videoHelper) return;

    if (m_shareSourceIds.empty()) {
        if (m_shareSubscribed) {
            m_videoHelper->unSubscribe();
            m_shareSubscribed = false;
            Util::Logger::getInstance().success("Unsubscribed from share (no active sources)");
        }
        return;
    }

    const unsigned int newTop = m_shareSourceIds.back();

    if (m_shareSubscribed) {
        m_videoHelper->unSubscribe();
        m_shareSubscribed = false;
    }

    auto err = m_videoHelper->subscribe(newTop, RAW_DATA_TYPE_SHARE);
    if (hasError(err, "subscribe to share source " + std::to_string(newTop))) {
        return;
    }

    m_shareSubscribed = true;
    Util::Logger::getInstance().success("Subscribed to share source " + std::to_string(newTop));
}
