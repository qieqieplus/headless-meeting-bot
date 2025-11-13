#include "zoom_bot_audio_delegate.h"

#include "MediaConfig.h"
#include "MediaController.h"

void ZoomBotAudioRawDataDelegate::onMixedAudioRawDataReceived(AudioRawData* data) {
  if (!data || !media_controller_) {
    return;
  }
  char* buffer = data->GetBuffer();
  unsigned int length = data->GetBufferLen();
  unsigned int sampleRate = data->GetSampleRate();
  unsigned int channels = data->GetChannelNum();
  uint64_t timestamp = data->GetTimeStamp();

  if (buffer && length > 0) {
    auto* pcmData = reinterpret_cast<const uint8_t*>(buffer);
    media_controller_->DispatchAudio(pcmData, length, sampleRate, channels, ZOOM_AUDIO_TYPE_MIXED,
                                     0, timestamp);
  }
}

void ZoomBotAudioRawDataDelegate::onOneWayAudioRawDataReceived(AudioRawData* data,
                                                               uint32_t user_id) {
  if (!data || !media_controller_) {
    return;
  }
  char* buffer = data->GetBuffer();
  unsigned int length = data->GetBufferLen();
  unsigned int sampleRate = data->GetSampleRate();
  unsigned int channels = data->GetChannelNum();
  uint64_t timestamp = data->GetTimeStamp();

  if (buffer && length > 0) {
    auto* pcmData = reinterpret_cast<const uint8_t*>(buffer);
    media_controller_->DispatchAudio(pcmData, length, sampleRate, channels, ZOOM_AUDIO_TYPE_ONE_WAY,
                                     user_id, timestamp);
  }
}

void ZoomBotAudioRawDataDelegate::onShareAudioRawDataReceived(AudioRawData* data,
                                                              uint32_t user_id) {
  if (!data || !media_controller_) {
    return;
  }
  char* buffer = data->GetBuffer();
  unsigned int length = data->GetBufferLen();
  unsigned int sampleRate = data->GetSampleRate();
  unsigned int channels = data->GetChannelNum();
  uint64_t timestamp = data->GetTimeStamp();

  if (buffer && length > 0) {
    auto* pcmData = reinterpret_cast<const uint8_t*>(buffer);
    media_controller_->PushAudioPCM(StreamKind::kShare, pcmData, length, sampleRate, channels,
                                    timestamp);
  }
}

void ZoomBotAudioRawDataDelegate::onOneWayInterpreterAudioRawDataReceived(AudioRawData* data,
                                                                          const zchar_t* lang) {}
