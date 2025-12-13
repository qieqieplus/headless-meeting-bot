
#pragma once

#include <mutex>
#include <optional>
#include <unordered_map>

#include "rawdata/rawdata_audio_helper_interface.h"
#include "util/ActiveSpeaker.h"
#include "zoom_bot_c.h"
#include "zoom_sdk_raw_data_def.h"

// Forward declarations
class MediaController;
class UserController;

/**
 * VAD detection result enum.
 */
enum class VADResult {
  Skipped,
  Speaking,    // VAD detected speech
  NotSpeaking  // VAD detected no speech
};

/**
 * This class encapsulates all Voice Activity Detection (VAD) functionality,
 * managing per-user audio buffers and detectors
 */
class AudioVADProcessor {
 public:
  AudioVADProcessor() = default;

  /**
   * @brief Processes audio data through VAD for a specific user.
   * @param pcm_data Raw PCM audio data
   * @param pcm_length Length of the PCM data in bytes
   * @param sample_rate Sample rate of the audio
   * @param channels Number of audio channels
   * @param user_id User identifier
   * @return VADResult indicating the detection result
   */
  VADResult ProcessVAD(const uint8_t* pcm_data, size_t pcm_length, uint32_t sample_rate,
                       uint32_t channels, uint32_t user_id);

 private:
  // Audio buffer for accumulating samples before VAD processing
  struct AudioBuffer {
    std::vector<int16_t> samples;
    uint32_t sample_rate = 0;
    uint32_t channels = 0;
    static constexpr size_t kTargetDurationMs = 30;  // Target 30ms chunks for VAD

    size_t GetTargetSampleCount() const { return (sample_rate * kTargetDurationMs) / 1000; }

    bool HasEnoughSamples() const { return samples.size() >= GetTargetSampleCount(); }
  };

  // Accumulate incoming audio samples into per-user buffer
  void AccumulateAudioSamples(AudioBuffer& buffer, const int16_t* pcm_samples, size_t num_samples,
                              uint32_t channels);

  // Get or create VAD detector for a user
  vad::ActiveSpeakerDetector& GetOrCreateDetector(uint32_t user_id, uint32_t sample_rate);

  // Active speaker detection: per-user VAD state
  mutable std::mutex vad_mutex_;
  std::unordered_map<unsigned int, vad::ActiveSpeakerDetector> parallel_detectors_;
  std::unordered_map<unsigned int, AudioBuffer> audio_buffers_;
};

class ZoomBotAudioRawDataDelegate : public ZOOMSDK::IZoomSDKAudioRawDataDelegate {
 public:
  ZoomBotAudioRawDataDelegate(MediaController* media_controller, UserController* user_controller);

  void onMixedAudioRawDataReceived(AudioRawData* data) override;
  void onOneWayAudioRawDataReceived(AudioRawData* data, uint32_t user_id) override;
  void onShareAudioRawDataReceived(AudioRawData* data, uint32_t user_id) override;
  void onOneWayInterpreterAudioRawDataReceived(AudioRawData* data, const zchar_t* lang) override;

 private:
  MediaController* media_controller_;
  UserController* user_controller_;
  AudioVADProcessor vad_processor_;

  // Track previous speaking state to detect transitions
  mutable std::mutex speaking_state_mutex_;
  std::unordered_map<unsigned int, bool> previous_speaking_state_;
};
