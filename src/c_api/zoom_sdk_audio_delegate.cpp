#include "zoom_sdk_audio_delegate.h"

void ZoomSDKAudioRawDataDelegate::onMixedAudioRawDataReceived(AudioRawData* data) {
    if (!data) return;
    char* buffer = data->GetBuffer();
    unsigned int length = data->GetBufferLen();
    if (buffer && length > 0) {
        zoom_meeting_dispatch_audio(m_meetingHandle, buffer, static_cast<int>(length), ZOOM_AUDIO_TYPE_MIXED, 0);
    }
}

void ZoomSDKAudioRawDataDelegate::onOneWayAudioRawDataReceived(AudioRawData* data, uint32_t user_id) {
    if (!data) return;
    char* buffer = data->GetBuffer();
    unsigned int length = data->GetBufferLen();
    if (buffer && length > 0) {
        zoom_meeting_dispatch_audio(m_meetingHandle, buffer, static_cast<int>(length), ZOOM_AUDIO_TYPE_ONE_WAY, user_id);
    }
}

void ZoomSDKAudioRawDataDelegate::onShareAudioRawDataReceived(AudioRawData *data, uint32_t user_id) {}

void ZoomSDKAudioRawDataDelegate::onOneWayInterpreterAudioRawDataReceived(AudioRawData *data, const zchar_t *lang) {}
