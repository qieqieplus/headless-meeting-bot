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

constexpr const char* kCameraSuffix = "user";
constexpr const char* kShareSuffix = "share";

struct StreamKey {
  StreamKind kind;
  unsigned int id;
  bool operator==(const StreamKey& other) const { return kind == other.kind && id == other.id; }
};

struct StreamKeyHash {
  size_t operator()(const StreamKey& key) const {
    return std::hash<int>()(static_cast<int>(key.kind)) ^ (std::hash<unsigned int>()(key.id) << 1);
  }
};

class VideoConfig {
 public:
  using HlsFileCallback = std::function<void(const char* filename, const uint8_t* data, size_t size,
                                             int is_playlist, uint64_t sequence)>;

  VideoConfig();
  ~VideoConfig();

  // Setters
  void SetHlsMediaCallback(const VideoEncoderConfig& v, const AudioEncoderConfig& a,
                           const HlsMuxerConfig& m, HlsFileCallback cb);
  void ClearHlsMediaParams();

  // Getters
  HlsFileCallback GetHlsFileCallback() const;
  std::unique_ptr<AudioEncoderConfig> CreateAudioConfig() const;
  std::unique_ptr<VideoEncoderConfig> CreateVideoConfig(const StreamKey& key) const;
  std::unique_ptr<HlsMuxerConfig> CreateHlsConfig(const StreamKey& key) const;
  std::string GetStreamType(const StreamKey& key) const;

 private:
  mutable std::mutex mtx_;
  HlsFileCallback hls_file_callback_;

  std::unique_ptr<VideoEncoderConfig> video_cfg_;
  std::unique_ptr<AudioEncoderConfig> audio_cfg_;
  std::unique_ptr<HlsMuxerConfig> muxer_cfg_;
};
