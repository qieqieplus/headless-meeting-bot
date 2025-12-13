#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

// Audio stream key
struct AudioStreamKey {
  int audio_type;  // 0=mixed, 1=user, 2=share
  uint32_t user_id;

  bool operator==(const AudioStreamKey& other) const {
    return audio_type == other.audio_type && user_id == other.user_id;
  }
};

struct AudioStreamKeyHash {
  size_t operator()(const AudioStreamKey& key) const {
    return std::hash<int>()(key.audio_type) ^ (std::hash<uint32_t>()(key.user_id) << 1);
  }
};

class AudioConfig {
 public:
  // Unified audio callback for both raw PCM and encoded audio (MP3/AAC)
  // format: "pcm", "mp3", or "aac"
  using AudioCallback = std::function<void(
      const uint8_t* data, size_t size, const char* format, uint32_t sample_rate, uint32_t channels,
      int audio_type, uint32_t user_id, uint64_t timestamp_ms, const char* filename)>;

  struct AudioEncodingConfig {
    int sample_rate = 32000;
    int channels = 1;
    std::string codec = "aac";  // "aac" or "mp3"
    int bitrate_kbps = 128;
  };

  AudioConfig();
  ~AudioConfig();

  // Setters
  void SetAudioCallback(AudioCallback cb, const AudioEncodingConfig* encoding_config = nullptr);
  void ClearAudioCallback();

  // Getters
  AudioCallback GetAudioCallback() const;
  std::unique_ptr<AudioEncodingConfig> GetAudioEncodingConfig() const;
  std::string GetAudioFilename(int audio_type, uint32_t user_id, uint64_t timestamp_ms,
                               const std::string& codec) const;
  std::string GetStreamType(const AudioStreamKey& key) const;

 private:
  mutable std::mutex mtx_;
  AudioCallback audio_callback_;
  std::unique_ptr<AudioEncodingConfig> audio_encoding_cfg_;
};
