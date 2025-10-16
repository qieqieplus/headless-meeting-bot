
#ifndef ZOOM_SDK_AUDIO_DELEGATE_H
#define ZOOM_SDK_AUDIO_DELEGATE_H

#include <iostream>
#include "zoom_sdk_raw_data_def.h"
#include "rawdata/rawdata_audio_helper_interface.h"
#include "zoom_sdk_c.h"

using namespace std;
using namespace ZOOMSDK;

class ZoomSDKAudioRawDataDelegate : public IZoomSDKAudioRawDataDelegate {
public:
    explicit ZoomSDKAudioRawDataDelegate(MeetingHandle meetingHandle)
        : m_meetingHandle(meetingHandle) {}

    void onMixedAudioRawDataReceived(AudioRawData* data) override;
    void onOneWayAudioRawDataReceived(AudioRawData* data, uint32_t user_id) override;
    void onShareAudioRawDataReceived(AudioRawData* data, uint32_t user_id) override;
    void onOneWayInterpreterAudioRawDataReceived(AudioRawData* data, const zchar_t* lang) override;

private:
    MeetingHandle m_meetingHandle;
};

#endif // ZOOM_SDK_AUDIO_DELEGATE_H
