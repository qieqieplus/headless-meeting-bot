#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

struct VideoEncoderConfig;
struct AudioEncoderConfig;
struct HlsMuxerConfig;

enum class StreamKind { kCamera, kShare };

constexpr const char* kCameraSuffix = "cam-";
constexpr const char* kShareSuffix = "share-";

struct StreamKey {
  StreamKind kind;
  unsigned int id;
  bool operator==(const StreamKey& other) const { return kind == other.kind && id == other.id; }
};

namespace std {
template <>
struct hash<StreamKey> {
  size_t operator()(const StreamKey& key) const {
    return std::hash<int>()(static_cast<int>(key.kind)) ^ (std::hash<unsigned int>()(key.id) << 1);
  }
};
}  // namespace std

class MediaConfig {
 public:
  using HlsFileCallback = std::function<void(const char* filename, const uint8_t* data, size_t size,
                                             int is_playlist, uint64_t sequence)>;
  using AudioCallback = std::function<void(const uint8_t* pcm_data, size_t pcm_length,
                                           uint32_t sample_rate, uint32_t channels, int audio_type,
                                           uint32_t user_id, uint64_t timestamp_ms)>;

  MediaConfig() = default;
  explicit MediaConfig(std::string meeting_id);
  ~MediaConfig() = default;

  // Setters
  void SetAudioCallback(AudioCallback cb);
  void ClearAudioCallback();
  void SetHlsMediaCallback(const VideoEncoderConfig& v, const AudioEncoderConfig& a,
                           const HlsMuxerConfig& m, HlsFileCallback cb);
  void ClearHlsMediaParams();

  // Getters - return copies/snapshots for safe use without external locks
  AudioCallback GetAudioCallback() const;
  HlsFileCallback GetHlsFileCallback() const;

  // Stream-specific config getters that apply stream-type modifications
  std::unique_ptr<AudioEncoderConfig> GetAudioEncoderConfig() const;
  std::unique_ptr<VideoEncoderConfig> GetVideoEncoderConfigForStream(const StreamKey& key) const;
  std::unique_ptr<HlsMuxerConfig> GetMuxerConfigForStream(const StreamKey& key) const;
  std::string GetStreamSuffix(const StreamKey& key) const;

 private:
  mutable std::mutex mtx_;
  AudioCallback audio_callback_;
  HlsFileCallback hls_file_callback_;
  std::unique_ptr<VideoEncoderConfig> video_cfg_;
  std::unique_ptr<AudioEncoderConfig> audio_cfg_;
  std::unique_ptr<HlsMuxerConfig> muxer_cfg_;
  std::string meeting_id_;
};
