#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

// =============================================================================
// A header-only C++20 library for efficient,
// non-AI based active speaker detection in a PCM audio stream.
//
// Expects mono PCM16 audio. If stereo arrives, downmix to mono upstream.

namespace vad {

// Defines a non-owning view of a segment of 16-bit PCM audio data.
using AudioFrame = std::span<const int16_t>;

/**
 * @brief A collection of stateless, efficient signal processing utilities.
 */
namespace detail {

/**
 * @brief Fused feature extraction: pre-emphasis, RMS energy, and ZCR in one pass.
 *
 * Avoids per-frame allocations by computing all features in a single loop.
 *
 * @param frame The input audio frame.
 * @param last_sample The last sample from the previous frame (updated in-place).
 * @param pre_emphasis_coeff The pre-emphasis filter coefficient (typically ~0.97).
 * @param zcr_epsilon Minimum absolute value to consider for zero-crossing (avoids noise).
 * @param[out] out_energy The computed RMS energy.
 * @param[out] out_zcr_rate The computed zero-crossing rate (normalized to [0, 1]).
 */
inline void ExtractFeatures(AudioFrame frame, int16_t& last_sample, float pre_emphasis_coeff,
                            float zcr_epsilon, float& out_energy, float& out_zcr_rate) noexcept {
  if (frame.empty()) {
    out_energy = 0.0f;
    out_zcr_rate = 0.0f;
    return;
  }

  double sum_of_squares = 0.0;
  size_t crossings = 0;
  float prev_emphasized =
      static_cast<float>(frame[0]) - pre_emphasis_coeff * static_cast<float>(last_sample);
  sum_of_squares += static_cast<double>(prev_emphasized) * prev_emphasized;

  for (size_t i = 1; i < frame.size(); ++i) {
    const float curr_emphasized =
        static_cast<float>(frame[i]) - pre_emphasis_coeff * static_cast<float>(frame[i - 1]);
    sum_of_squares += static_cast<double>(curr_emphasized) * curr_emphasized;

    // Count zero-crossings with epsilon guard.
    if (std::fabs(prev_emphasized) > zcr_epsilon && std::fabs(curr_emphasized) > zcr_epsilon &&
        (prev_emphasized * curr_emphasized) < 0.0f) {
      crossings++;
    }
    prev_emphasized = curr_emphasized;
  }

  last_sample = frame.back();
  out_energy = std::sqrt(static_cast<float>(sum_of_squares / frame.size()));
  out_zcr_rate = (frame.size() > 1) ? static_cast<float>(crossings) / (frame.size() - 1) : 0.0f;
}

}  // namespace detail

/**
 * @brief Core Voice Activity Detector (VAD) engine.
 *
 * This class maintains the state required to distinguish between speech and
 * non-speech audio segments using absolute thresholds and a state
 * machine for smoothing.
 */
class VoiceActivityDetector {
 public:
  struct Config {
    // === Signal Processing ===
    // Pre-emphasis filter coefficient.
    float pre_emphasis_coeff = 0.97f;
    // Epsilon for zero-crossing detection (ignores tiny noise).
    float zcr_epsilon = 1e-3f;

    // === Speech Detection Thresholds (Hysteresis) ===
    // Absolute energy threshold for activation (onset).
    float energy_threshold_onset = 500.0f;
    // Absolute energy threshold for continuation (offset, lower = easier to stay active).
    float energy_threshold_offset = 300.0f;
    // Absolute ZCR threshold for activation (onset).
    float zcr_threshold_onset = 0.05f;
    // Absolute ZCR threshold for continuation (offset).
    float zcr_threshold_offset = 0.03f;

    // === State Machine Timing ===
    // Number of consecutive speech frames to trigger the 'active' state.
    size_t speech_onset_frames = 3;
    // Number of consecutive silence frames to trigger the 'inactive' state.
    size_t silence_termination_frames = 10;
    // Minimum frames to hold active state before allowing deactivation (0 = disabled).
    size_t min_active_frames_hold = 0;
    // Minimum frames to hold inactive state before allowing activation (0 = disabled).
    size_t min_inactive_frames_hold = 0;
  };

  VoiceActivityDetector() : VoiceActivityDetector(Config{}) {}
  explicit VoiceActivityDetector(const Config& config);

  /**
   * @brief Processes a single audio frame to update the VAD state.
   * @param frame A non-owning view of the PCM16 audio data.
   * @return True if the current state is considered speech, false otherwise.
   */
  inline bool ProcessFrame(AudioFrame frame);

  /**
   * @brief Returns true if an active speaker is currently detected.
   * @return The smoothed active speaker state.
   */
  inline bool IsActive() const noexcept;

  /**
   * @brief Resets the detector state for reuse with a new audio stream.
   */
  inline void Reset() noexcept;

 private:
  const Config config_;

  // Pre-emphasis state.
  int16_t last_sample_ = 0;

  // State machine for smoothing.
  bool is_active_ = false;
  size_t consecutive_speech_frames_ = 0;
  size_t consecutive_silence_frames_ = 0;
  size_t frames_since_state_change_ = 0;
};

// --- Implementation of VoiceActivityDetector ---

inline VoiceActivityDetector::VoiceActivityDetector(const Config& config) : config_(config) {}

inline bool VoiceActivityDetector::IsActive() const noexcept { return is_active_; }

inline void VoiceActivityDetector::Reset() noexcept {
  last_sample_ = 0;
  is_active_ = false;
  consecutive_speech_frames_ = 0;
  consecutive_silence_frames_ = 0;
  frames_since_state_change_ = 0;
}

inline bool VoiceActivityDetector::ProcessFrame(AudioFrame frame) {
  if (frame.empty()) {
    return false;
  }

  // 1. Extract features in a single fused pass (no allocations).
  float energy = 0.0f;
  float zcr_rate = 0.0f;
  detail::ExtractFeatures(frame, last_sample_, config_.pre_emphasis_coeff, config_.zcr_epsilon,
                          energy, zcr_rate);
  // std::cout << "energy: " << energy << ", zcr_rate: " << zcr_rate << std::endl;

  // 2. Make a preliminary decision for the current frame with hysteresis.
  const bool onset_candidate =
      (energy > config_.energy_threshold_onset) && (zcr_rate > config_.zcr_threshold_onset);
  const bool continue_candidate =
      (energy > config_.energy_threshold_offset) && (zcr_rate > config_.zcr_threshold_offset);

  const bool is_currently_speech = is_active_ ? continue_candidate : onset_candidate;

  // 3. Apply state machine for smoothing (hangover).
  frames_since_state_change_++;
  if (is_currently_speech) {
    consecutive_speech_frames_++;
    consecutive_silence_frames_ = 0;
    if (!is_active_ && frames_since_state_change_ >= config_.min_inactive_frames_hold &&
        consecutive_speech_frames_ >= config_.speech_onset_frames) {
      is_active_ = true;
      frames_since_state_change_ = 0;
    }
  } else {
    consecutive_silence_frames_++;
    consecutive_speech_frames_ = 0;
    if (is_active_ && frames_since_state_change_ >= config_.min_active_frames_hold &&
        consecutive_silence_frames_ >= config_.silence_termination_frames) {
      is_active_ = false;
      frames_since_state_change_ = 0;
    }
  }

  return is_active_;
}

/**
 * @brief High-level active speaker detector.
 *
 * Manages an audio stream, breaks it into frames, and uses a
 * VoiceActivityDetector instance to determine the presence of an active speaker.
 * This is the primary interface for clients.
 */
class ActiveSpeakerDetector {
 public:
  struct AudioConfig {
    uint32_t sample_rate = 32000;
    // Standard frame duration for voice processing.
    uint16_t frame_duration_ms = 30;
  };

  ActiveSpeakerDetector() : ActiveSpeakerDetector(AudioConfig{}, VoiceActivityDetector::Config{}) {}
  explicit ActiveSpeakerDetector(const AudioConfig& audio_config,
                                 const VoiceActivityDetector::Config& vad_config);

  /**
   * @brief Ingests a chunk of raw PCM audio data and processes it.
   * @param pcm_data Pointer to the start of the PCM16 audio data.
   * @param num_samples The number of samples in the buffer.
   */
  inline void ProcessPcmChunk(const int16_t* pcm_data, size_t num_samples);

  /**
   * @brief Returns true if an active speaker is currently detected.
   * @return The current active speaker state.
   */
  inline bool IsActiveSpeaker() const noexcept;

  /**
   * @brief Resets the detector state for reuse with a new audio stream.
   */
  inline void Reset() noexcept;

 private:
  const AudioConfig audio_config_;
  const size_t samples_per_frame_;
  VoiceActivityDetector vad_;
  std::vector<int16_t> internal_buffer_;
};

// --- Implementation of ActiveSpeakerDetector ---

inline ActiveSpeakerDetector::ActiveSpeakerDetector(const AudioConfig& audio_config,
                                                    const VoiceActivityDetector::Config& vad_config)
    : audio_config_(audio_config),
      samples_per_frame_((audio_config.sample_rate * audio_config.frame_duration_ms) / 1000),
      vad_(vad_config) {
  if ((audio_config.sample_rate * audio_config.frame_duration_ms) % 1000 != 0) {
    throw std::invalid_argument(
        "Sample rate and frame duration must result in an integer number of "
        "samples per frame.");
  }
}

inline void ActiveSpeakerDetector::ProcessPcmChunk(const int16_t* pcm_data, size_t num_samples) {
  if (num_samples == 0) {
    return;
  }

  // Append new data to the internal buffer.
  internal_buffer_.insert(internal_buffer_.end(), pcm_data, pcm_data + num_samples);

  // Process all full frames available in the buffer.
  size_t offset = 0;
  while (offset + samples_per_frame_ <= internal_buffer_.size()) {
    // Create a non-owning span for the frame.
    AudioFrame current_frame(internal_buffer_.data() + offset, samples_per_frame_);

    // Process it with the VAD engine.
    vad_.ProcessFrame(current_frame);

    offset += samples_per_frame_;
  }

  // Compact the buffer: move remaining samples to the front.
  if (offset > 0) {
    const size_t remaining = internal_buffer_.size() - offset;
    if (remaining > 0) {
      std::move(internal_buffer_.begin() + offset, internal_buffer_.end(),
                internal_buffer_.begin());
    }
    internal_buffer_.resize(remaining);
  }
}

inline bool ActiveSpeakerDetector::IsActiveSpeaker() const noexcept { return vad_.IsActive(); }

inline void ActiveSpeakerDetector::Reset() noexcept {
  vad_.Reset();
  internal_buffer_.clear();
}

}  // namespace vad
