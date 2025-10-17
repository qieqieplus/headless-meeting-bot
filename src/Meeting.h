#ifndef HEADLESS_ZOOM_BOT_MEETING_H
#define HEADLESS_ZOOM_BOT_MEETING_H

#include <iostream>
#include <functional>
#include <string>
#include <memory>

#include "MeetingConfig.h"

// SDK interfaces needed by implementation
#include "meeting_service_interface.h"
#include "meeting_service_components/meeting_audio_interface.h"
#include "meeting_service_components/meeting_participants_ctrl_interface.h"
#include "meeting_service_components/meeting_sharing_interface.h"
#include "meeting_service_components/meeting_video_interface.h"
#include "setting_service_interface.h"

// Raw data helpers
#include "rawdata/zoom_rawdata_api.h"
#include "rawdata/rawdata_audio_helper_interface.h"
#include "rawdata/rawdata_renderer_interface.h"

// Event implementations
#include "events/MeetingServiceEvent.h"
#include "events/MeetingReminderEvent.h"
#include "events/MeetingRecordingCtrlEvent.h"
#include "events/MeetingShareEvent.h"


class Meeting {

    MeetingConfig m_config;

    ZOOMSDK::IZoomSDKAudioRawDataHelper* m_audioHelper;
    ZOOMSDK::IZoomSDKAudioRawDataDelegate* m_audioSource;

    // Video support
    ZOOMSDK::IZoomSDKRenderer* m_videoHelper;
    ZOOMSDK::IZoomSDKRendererDelegate* m_videoSource;

    bool m_isJoined;
    bool m_isRecording;

    // Service references (injected instead of obtained from singleton)
    ZOOMSDK::IMeetingService* m_meetingService;
    ZOOMSDK::ISettingService* m_settingService;

    // Event object ownership
    std::unique_ptr<MeetingReminderEvent> m_reminderEvent;
    std::unique_ptr<MeetingRecordingCtrlEvent> m_recordingEvent;
    std::unique_ptr<MeetingServiceEvent> m_meetingServiceEvent;
    std::unique_ptr<MeetingShareEvent> m_shareEvent;

    // Share tracking
    unsigned int m_currentShareSourceId;
    bool m_shareSubscribed;

    ZOOMSDK::SDKError setupMeetingEvents();
    void subscribeShare(const ZOOMSDK::ZoomSDKSharingSourceInfo& shareInfo);
    void unSubscribeShare(const ZOOMSDK::ZoomSDKSharingSourceInfo& shareInfo);

public:
    Meeting(const MeetingConfig& config, ZOOMSDK::IMeetingService* meetingService, ZOOMSDK::ISettingService* settingService);
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
    
    const MeetingConfig& getConfig() const { return m_config; }
    ZOOMSDK::IMeetingService* getMeetingService() const { return m_meetingService; }

    void setAudioSource(ZOOMSDK::IZoomSDKAudioRawDataDelegate* source) { m_audioSource = source; }
    ZOOMSDK::IZoomSDKAudioRawDataDelegate* getAudioSource() const { return m_audioSource; }
    
    void setVideoSource(ZOOMSDK::IZoomSDKRendererDelegate* source) { m_videoSource = source; }
    ZOOMSDK::IZoomSDKRendererDelegate* getVideoSource() const { return m_videoSource; }

    // Static factory methods
    static Meeting* createMeeting(const MeetingConfig& meetingConfig, ZOOMSDK::IMeetingService* meetingService, ZOOMSDK::ISettingService* settingService);
    static Meeting* createMeeting(const std::string& meetingId,
                                  const std::string& password,
                                  const std::string& displayName,
                                  bool isMeetingStart,
                                  const std::string& joinToken,
                                  bool useRawAudio,
                                  bool useRawVideo,
                                  ZOOMSDK::IMeetingService* meetingService,
                                  ZOOMSDK::ISettingService* settingService);

    // Static utility methods
    static bool hasError(ZOOMSDK::SDKError e, const std::string& action="");
};

#endif //HEADLESS_ZOOM_BOT_MEETING_H
