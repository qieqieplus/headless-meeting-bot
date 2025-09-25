#ifndef HEADLESS_ZOOM_BOT_MEETING_H
#define HEADLESS_ZOOM_BOT_MEETING_H

#include <iostream>
#include <functional>
#include <string>

#include "MeetingConfig.h"

#include "zoom_sdk.h"
#include "rawdata/zoom_rawdata_api.h"
#include "rawdata/rawdata_audio_helper_interface.h"

#include "meeting_service_components/meeting_audio_interface.h"
#include "meeting_service_components/meeting_participants_ctrl_interface.h"
#include "meeting_service_interface.h"
#include "setting_service_interface.h"

#include "events/MeetingServiceEvent.h"
#include "events/MeetingReminderEvent.h"
#include "events/MeetingRecordingCtrlEvent.h"

using namespace std;
using namespace ZOOMSDK;


class Meeting {

    MeetingConfig m_config;

    IZoomSDKAudioRawDataHelper* m_audioHelper;
    IZoomSDKAudioRawDataDelegate* m_audioSource;

    // Video support (placeholder for now)
    void* m_videoHelper;
    void* m_videoSource;

    bool m_isJoined;
    bool m_isRecording;

    // Service references (injected instead of obtained from singleton)
    IMeetingService* m_meetingService;
    ISettingService* m_settingService;

    SDKError setupMeetingEvents();

public:
    Meeting(const MeetingConfig& config, IMeetingService* meetingService, ISettingService* settingService);
    ~Meeting();

    SDKError join();
    SDKError start();
    SDKError leave();

    SDKError startOrJoin();
    SDKError startRawRecording();
    SDKError stopRawRecording();

    bool isMeetingStart() const;
    bool isJoined() const { return m_isJoined; }
    bool isRecording() const { return m_isRecording; }
    
    const MeetingConfig& getConfig() const { return m_config; }

    void setAudioSource(IZoomSDKAudioRawDataDelegate* source) { m_audioSource = source; }
    IZoomSDKAudioRawDataDelegate* getAudioSource() const { return m_audioSource; }

    // Static factory methods
    static Meeting* createMeeting(const MeetingConfig& meetingConfig, IMeetingService* meetingService, ISettingService* settingService);
    static Meeting* createMeeting(const string& meetingId,
                                  const string& password,
                                  const string& displayName,
                                  bool isMeetingStart,
                                  const string& joinToken,
                                  bool useRawAudio,
                                  bool useRawVideo,
                                  IMeetingService* meetingService,
                                  ISettingService* settingService);

    // Static utility methods
    static bool hasError(SDKError e, const string& action="");
};

#endif //HEADLESS_ZOOM_BOT_MEETING_H
