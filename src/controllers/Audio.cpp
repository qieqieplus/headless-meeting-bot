#include "controllers/Audio.h"

#include <chrono>

#include "rawdata/zoom_rawdata_api.h"
#include "video/AudioEncodePipeline.h"
#include "video/AudioEncoder.h"

using namespace ZOOMSDK;

AudioController::AudioController(AudioConfig& config, TimelineClock& timeline_clock)
    : config_(config),
      timeline_clock_(timeline_clock),
      is_recording_(false),
      audio_delegate_(nullptr) {}

AudioController::~AudioController() noexcept { StopRecording(); }

uint64_t AudioController::GetCurrentTimestampMs() const {
  using clock = std::chrono::system_clock;
  return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch())
      .count();
}

void AudioController::LogStreamEvent(const AudioStreamKey& key, const std::string& event,
                                     const std::string& level) {
  std::string msg = event + " " + config_.GetStreamType(key);
  ::LogStreamEvent(msg, level);
}

bool AudioController::ActivateAudioComponents(AudioStreamState& state, const AudioStreamKey& key) {
  // Create audio helper (global subscription) if not already created
  if (!audio_helper_ && !CreateAudioHelper(state, key)) {
    LogStreamEvent(key, "Failed to create audio helper", "error");
    return false;
  }

  // Create pipeline or raw PCM filename based on encoding config
  auto audio_cfg = config_.GetAudioEncodingConfig();
  if (audio_cfg) {
    if (!state.pipeline && !CreateAudioPipeline(state, key)) {
      LogStreamEvent(key, "Failed to create pipeline", "error");
      return false;
    }
  } else {
    SetRawPcmFilename(state, key.audio_type, key.user_id);
  }

  // Ensure subscription is active (if helper exists)
  if (audio_helper_ && !state.is_subscribed) {
    SubscribeAudioHelper(state, key);
  }

  return true;
}

bool AudioController::CreateAudioHelper(AudioStreamState& state, const AudioStreamKey& key) {
  if (audio_helper_) {
    return true;  // Already created
  }

  if (!audio_delegate_factory_) {
    LogStreamEvent(key, "No audio delegate factory set", "warn");
    return false;
  }

  auto* delegate = audio_delegate_factory_();
  if (!delegate) {
    LogStreamEvent(key, "Factory returned null delegate", "error");
    return false;
  }

  auto* helper = GetAudioRawdataHelper();
  if (!helper) {
    LogStreamEvent(key, "Failed to get audio helper", "error");
    delete delegate;
    return false;
  }

  SDKError err = helper->subscribe(delegate);
  if (err != SDKERR_SUCCESS) {
    LogStreamEvent(key, "Failed to subscribe to audio helper (error: " + std::to_string(err) + ")",
                   "error");
    delete delegate;
    return false;
  }

  audio_helper_.reset(helper);
  audio_delegate_ = delegate;
  LogStreamEvent(key, "Subscribed to audio helper");
  return true;
}

bool AudioController::CreateAudioPipeline(AudioStreamState& state, const AudioStreamKey& key) {
  if (state.pipeline) {
    return true;  // Already exists
  }

  auto audio_cfg = config_.GetAudioEncodingConfig();
  if (!audio_cfg) {
    LogStreamEvent(key, "No audio encoding config; cannot create pipeline", "warn");
    return false;
  }

  auto pipeline = std::make_shared<AudioEncodePipeline>();
  AudioEncoderConfig encoder_cfg;
  encoder_cfg.sample_rate = audio_cfg->sample_rate;
  encoder_cfg.channels = audio_cfg->channels;
  encoder_cfg.codec = audio_cfg->codec;
  encoder_cfg.bitrate_kbps = audio_cfg->bitrate_kbps;

  uint64_t timestamp = GetCurrentTimestampMs();
  std::string filename =
      config_.GetAudioFilename(key.audio_type, key.user_id, timestamp, audio_cfg->codec);

  // Initialize timeline from first audio file timestamp
  timeline_clock_.SetBaseFromPts(timestamp);

  auto user_callback = config_.GetAudioCallback();

  auto callback = [user_callback, audio_type = key.audio_type, user_id = key.user_id, filename,
                   sample_rate = audio_cfg->sample_rate, channels = audio_cfg->channels,
                   codec = audio_cfg->codec](const uint8_t* data, size_t size,
                                             uint64_t timestamp_ms) {
    if (user_callback) {
      user_callback(data, size, codec.c_str(), sample_rate, channels, audio_type, user_id,
                    timestamp_ms, filename.c_str());
    }
  };

  if (pipeline->Start(encoder_cfg, callback)) {
    state.pipeline = pipeline;
    state.filename = filename;
    LogStreamEvent(key, "Started pipeline: " + filename);
    return true;
  } else {
    LogStreamEvent(key, "Failed to start pipeline: " + filename, "error");
    return false;
  }
}

void AudioController::SubscribeAudioHelper(AudioStreamState& state, const AudioStreamKey& key) {
  if (!audio_helper_) {
    return;
  }

  if (state.is_subscribed) {
    return;  // Already subscribed
  }

  state.is_subscribed = true;
  LogStreamEvent(key, "Marked as subscribed to audio helper");
}

void AudioController::SetRawPcmFilename(AudioStreamState& state, int audio_type, uint32_t user_id) {
  if (!state.filename.empty()) {
    return;
  }
  uint64_t timestamp = GetCurrentTimestampMs();
  state.filename = config_.GetAudioFilename(audio_type, user_id, timestamp, "pcm");

  // Initialize timeline from first audio file timestamp
  timeline_clock_.SetBaseFromPts(timestamp);

  AudioStreamKey key{audio_type, user_id};
  LogStreamEvent(key, "Started raw PCM: " + state.filename);
}

void AudioController::UpdateAudioStatus(unsigned int user_id, ZOOMSDK::AudioStatus status) {
  // Only handle user audio (audio_type=1), not mixed audio (audio_type=0)
  const int kUserAudioType = 1;

  // Check if user is muted (AUDIO_STATUS_MUTED = 1, AUDIO_STATUS_UNMUTED = 2)
  const bool is_muted = (status == ZOOMSDK::Audio_Muted);

  if (is_muted) {
    DestroyAudioStream(kUserAudioType, user_id);
  } else {
    EnsureAudioStream(kUserAudioType, user_id);
  }
}

void AudioController::PushAudioFrame(const uint8_t* pcm_data, size_t pcm_length,
                                     uint32_t sample_rate, uint32_t channels, int audio_type,
                                     uint32_t user_id, uint64_t timestamp_ms) {
  AudioStreamKey key{audio_type, user_id};
  AudioStreamState state;

  if (!GetStreamState(key, state)) {
    return;
  }

  if (state.pipeline) {
    state.pipeline->PushAudioPcm(pcm_data, pcm_length, sample_rate, channels, timestamp_ms);
  } else if (!state.filename.empty()) {
    auto audio_callback = config_.GetAudioCallback();
    if (audio_callback && pcm_data && pcm_length > 0) {
      audio_callback(pcm_data, pcm_length, "pcm", sample_rate, channels, audio_type, user_id,
                     timestamp_ms, state.filename.c_str());
    }
  }
}

void AudioController::ActivateAudioStreams() {
  // Activate all existing streams
  auto keys = GetAllKeys();
  for (const auto& key : keys) {
    UpdateStreamState(
        key, [this, &key](AudioStreamState& state) { ActivateAudioComponents(state, key); });
  }
}

void AudioController::EnsureAudioStream(int audio_type, uint32_t user_id) {
  AudioStreamKey key{audio_type, user_id};

  bool is_new = EnsureStreamExists(key);
  if (!is_new) {
    return;
  }

  LogStreamEvent(key, "Prepared stream");

  if (is_recording_.load(std::memory_order_acquire)) {
    UpdateStreamState(
        key, [this, &key](AudioStreamState& state) { ActivateAudioComponents(state, key); });
  }
}

void AudioController::DestroyAudioStream(int audio_type, uint32_t user_id) {
  AudioStreamState state;
  AudioStreamKey key{audio_type, user_id};

  if (!RemoveStream(key, state)) {
    return;
  }

  if (state.pipeline) {
    state.pipeline->Stop();
  }

  LogStreamEvent(key, "Destroyed stream");
}

void AudioController::StartRecording() {
  is_recording_.store(true, std::memory_order_release);

  // Ensure mixed audio stream (audio_type=0, user_id=0)
  // This is the master timeline
  EnsureAudioStream(0, 0);

  // Activate all existing streams
  ActivateAudioStreams();
}

void AudioController::StopRecording() {
  if (!is_recording_.load(std::memory_order_acquire)) {
    return;
  }
  is_recording_.store(false, std::memory_order_release);

  auto streams = ClearAllStreams();
  for (auto& [key, state] : streams) {
    if (state.pipeline) {
      state.pipeline->Stop();
    }
  }

  // Reset audio helper (will call unSubscribe via deleter)
  audio_helper_.reset();
  audio_delegate_ = nullptr;
}
