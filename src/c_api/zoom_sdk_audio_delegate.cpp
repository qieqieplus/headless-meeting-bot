#include "zoom_sdk_audio_delegate.h"
#include "zoom_sdk_internal.h"

void ZoomSDKAudioRawDataDelegate::onMixedAudioRawDataReceived(
    AudioRawData *data) {
  if (!data)
    return;
  char *buffer = data->GetBuffer();
  unsigned int length = data->GetBufferLen();
  unsigned int sampleRate = data->GetSampleRate();
  unsigned int channels = data->GetChannelNum();
  unsigned long long timestamp = data->GetTimeStamp();

  if (buffer && length > 0) {
    Impl::zoom_meeting_dispatch_mixed_audio(m_meetingHandle, buffer, length,
                                            sampleRate, channels, timestamp);
  }
}

void ZoomSDKAudioRawDataDelegate::onOneWayAudioRawDataReceived(
    AudioRawData *data, uint32_t user_id) {
  if (!data)
    return;
  char *buffer = data->GetBuffer();
  unsigned int length = data->GetBufferLen();
  unsigned int sampleRate = data->GetSampleRate();
  unsigned int channels = data->GetChannelNum();
  unsigned long long timestamp = data->GetTimeStamp();

  if (buffer && length > 0) {
    Impl::zoom_meeting_dispatch_one_way_audio(m_meetingHandle, buffer, length,
                                              sampleRate, channels, user_id,
                                              timestamp);
  }
}

void ZoomSDKAudioRawDataDelegate::onShareAudioRawDataReceived(
    AudioRawData *data, uint32_t user_id) {}

void ZoomSDKAudioRawDataDelegate::onOneWayInterpreterAudioRawDataReceived(
    AudioRawData *data, const zchar_t *lang) {}
