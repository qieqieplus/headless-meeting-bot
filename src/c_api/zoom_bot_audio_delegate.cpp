#include "zoom_bot_audio_delegate.h"

#include <utility>
#include <vector>

#include "controllers/AudioConfig.h"
#include "controllers/Media.h"
#include "controllers/User.h"
#include "util/Checks.h"

void AudioVADProcessor::AccumulateAudioSamples(AudioBuffer& buffer, const int16_t* pcm_samples,
                                               size_t num_samples, uint32_t channels) {
  if (channels == 2) {
    buffer.samples.reserve(buffer.samples.size() + num_samples / 2);
    for (size_t i = 0; i < num_samples; i += 2) {
      int32_t mixed =
          (static_cast<int32_t>(pcm_samples[i]) + static_cast<int32_t>(pcm_samples[i + 1])) / 2;
      buffer.samples.push_back(static_cast<int16_t>(mixed));
    }
  } else if (channels == 1) {
    buffer.samples.insert(buffer.samples.end(), pcm_samples, pcm_samples + num_samples);
  }
}

vad::ActiveSpeakerDetector& AudioVADProcessor::GetOrCreateDetector(uint32_t user_id,
                                                                   uint32_t sample_rate) {
  auto detector_it = parallel_detectors_.find(user_id);
  if (detector_it != parallel_detectors_.end()) {
    return detector_it->second;
  }

  vad::ActiveSpeakerDetector::AudioConfig audio_config = {.sample_rate = 32000,
                                                          .frame_duration_ms = 25};

  vad::VoiceActivityDetector::Config vad_config;

  // === Speech Detection Thresholds (Hysteresis) ===
  vad_config.energy_threshold_onset = 360.0f;   // energy for activation
  vad_config.energy_threshold_offset = 120.0f;  // energy to stay active
  vad_config.zcr_threshold_onset = 0.05f;       // ZCR for activation
  vad_config.zcr_threshold_offset = 0.03f;      // ZCR to stay active

  // === State Machine Timing ===
  vad_config.speech_onset_frames = 6;          // require brief consistent speech
  vad_config.silence_termination_frames = 40;  // ~1000ms hangover before inactive
  vad_config.min_active_frames_hold = 20;      // ~500ms after activation
  vad_config.min_inactive_frames_hold = 20;    // ~500ms after deactivation

  return parallel_detectors_.emplace(user_id, vad::ActiveSpeakerDetector(audio_config, vad_config))
      .first->second;
}

VADResult AudioVADProcessor::ProcessVAD(const uint8_t* pcm_data, size_t pcm_length,
                                        uint32_t sample_rate, uint32_t channels, uint32_t user_id) {
  if (user_id == 0) return VADResult::Skipped;

  if (channels != 1 && channels != 2) return VADResult::Skipped;

  std::lock_guard<std::mutex> vad_lock(vad_mutex_);

  auto& buffer = audio_buffers_[user_id];
  if (buffer.sample_rate == 0) {
    buffer.sample_rate = sample_rate;
    buffer.channels = channels;
  }

  const int16_t* pcm_samples = reinterpret_cast<const int16_t*>(pcm_data);
  size_t num_samples = pcm_length / sizeof(int16_t);
  AccumulateAudioSamples(buffer, pcm_samples, num_samples, channels);

  if (!buffer.HasEnoughSamples()) return VADResult::Skipped;

  auto& detector = GetOrCreateDetector(user_id, sample_rate);
  size_t target_samples = buffer.GetTargetSampleCount();
  detector.ProcessPcmChunk(buffer.samples.data(), target_samples);

  bool is_speaking = detector.IsActiveSpeaker();
  buffer.samples.erase(buffer.samples.begin(), buffer.samples.begin() + target_samples);

  return is_speaking ? VADResult::Speaking : VADResult::NotSpeaking;
}

ZoomBotAudioRawDataDelegate::ZoomBotAudioRawDataDelegate(MediaController* media_controller,
                                                         UserController* user_controller)
    : media_controller_(media_controller), user_controller_(user_controller) {
  ASSERT_NOT_NULL(media_controller_);
  ASSERT_NOT_NULL(user_controller_);
}

void ZoomBotAudioRawDataDelegate::onMixedAudioRawDataReceived(AudioRawData* data) {
  if (!data) return;

  char* buffer = data->GetBuffer();
  unsigned int length = data->GetBufferLen();
  unsigned int sampleRate = data->GetSampleRate();
  unsigned int channels = data->GetChannelNum();
  uint64_t timestamp = data->GetTimeStamp();

  if (buffer && length > 0) {
    auto* pcmData = reinterpret_cast<const uint8_t*>(buffer);
    media_controller_->PushAudioEncoded(pcmData, length, sampleRate, channels,
                                        ZOOM_AUDIO_TYPE_MIXED, 0, timestamp);
  }
}

void ZoomBotAudioRawDataDelegate::onOneWayAudioRawDataReceived(AudioRawData* data,
                                                               uint32_t user_id) {
  if (!data) return;

  // Filter out the bot's own audio - even though we're muted, we still receive our own stream
  if (user_id == user_controller_->GetBotUserId()) {
    return;
  }

  char* buffer = data->GetBuffer();
  unsigned int length = data->GetBufferLen();
  if (!buffer || length == 0) return;

  unsigned int sampleRate = data->GetSampleRate();
  unsigned int channels = data->GetChannelNum();
  uint64_t timestamp = data->GetTimeStamp();

  auto* pcmData = reinterpret_cast<const uint8_t*>(buffer);

  media_controller_->PushAudioEncoded(pcmData, length, sampleRate, channels,
                                      ZOOM_AUDIO_TYPE_ONE_WAY, user_id, timestamp);

  // Use VAD-based detection since Zoom's onUserActiveAudioChange callback is unreliable
  VADResult result = vad_processor_.ProcessVAD(pcmData, length, sampleRate, channels, user_id);
  if (result == VADResult::Skipped) return;

  bool is_speaking = (result == VADResult::Speaking);
  bool state_changed = false;
  {
    std::lock_guard<std::mutex> lock(speaking_state_mutex_);
    bool& prev_state = previous_speaking_state_[user_id];
    state_changed = std::exchange(prev_state, is_speaking) != is_speaking;
  }

  if (state_changed) {
    user_controller_->HandleSpeakingStatus(user_id, is_speaking);
  }
}

void ZoomBotAudioRawDataDelegate::onShareAudioRawDataReceived(AudioRawData* data,
                                                              uint32_t user_id) {
  if (!data) return;
  char* buffer = data->GetBuffer();
  unsigned int length = data->GetBufferLen();
  unsigned int sampleRate = data->GetSampleRate();
  unsigned int channels = data->GetChannelNum();
  uint64_t timestamp = data->GetTimeStamp();

  if (buffer && length > 0) {
    auto* pcmData = reinterpret_cast<const uint8_t*>(buffer);
    media_controller_->PushAudioToHlsPipelines(StreamKind::kShare, pcmData, length, sampleRate,
                                               channels, timestamp);
  }
}

void ZoomBotAudioRawDataDelegate::onOneWayInterpreterAudioRawDataReceived(AudioRawData* data,
                                                                          const zchar_t* lang) {}