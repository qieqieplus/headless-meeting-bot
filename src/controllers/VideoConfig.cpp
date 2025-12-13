#include "controllers/VideoConfig.h"

#include <chrono>
#include <iomanip>
#include <sstream>

#include "video/EncoderConfig.h"

VideoConfig::VideoConfig() = default;
VideoConfig::~VideoConfig() = default;

void VideoConfig::SetHlsMediaCallback(const VideoEncoderConfig& v, const AudioEncoderConfig& a,
                                      const HlsMuxerConfig& m, HlsFileCallback cb) {
  std::lock_guard<std::mutex> lock(mtx_);
  video_cfg_ = std::make_unique<VideoEncoderConfig>(v);
  audio_cfg_ = std::make_unique<AudioEncoderConfig>(a);
  muxer_cfg_ = std::make_unique<HlsMuxerConfig>(m);
  hls_file_callback_ = std::move(cb);
}

void VideoConfig::ClearHlsMediaParams() {
  std::lock_guard<std::mutex> lock(mtx_);
  video_cfg_.reset();
  audio_cfg_.reset();
  muxer_cfg_.reset();
  hls_file_callback_ = nullptr;
}

VideoConfig::HlsFileCallback VideoConfig::GetHlsFileCallback() const {
  std::lock_guard<std::mutex> lock(mtx_);
  return hls_file_callback_;
}

std::unique_ptr<AudioEncoderConfig> VideoConfig::CreateAudioConfig() const {
  std::lock_guard<std::mutex> lock(mtx_);
  if (!audio_cfg_) {
    return nullptr;
  }
  return std::make_unique<AudioEncoderConfig>(*audio_cfg_);
}

std::unique_ptr<VideoEncoderConfig> VideoConfig::CreateVideoConfig(const StreamKey& key) const {
  std::lock_guard<std::mutex> lock(mtx_);
  if (!video_cfg_) {
    return nullptr;
  }

  auto stream_video_cfg = std::make_unique<VideoEncoderConfig>(*video_cfg_);
  auto bitrate = (key.kind == StreamKind::kCamera) ? BitRate::kLow : BitRate::kHigh;
  auto gop_size = (key.kind == StreamKind::kShare) ? GOPSize::kLow : GOPSize::kHigh;
  stream_video_cfg->bitrate_kbps = static_cast<int>(bitrate);
  stream_video_cfg->gop_size = static_cast<int>(gop_size);

  return stream_video_cfg;
}

std::unique_ptr<HlsMuxerConfig> VideoConfig::CreateHlsConfig(const StreamKey& key) const {
  std::lock_guard<std::mutex> lock(mtx_);
  if (!muxer_cfg_) {
    return nullptr;
  }

  using clock = std::chrono::system_clock;
  auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                       clock::now().time_since_epoch())
                       .count();

  auto stream_mux_cfg = std::make_unique<HlsMuxerConfig>(*muxer_cfg_);
  const std::string suffix = GetStreamType(key);
  // Format: {user_id}_{type}_{timestamp}
  std::ostringstream oss;
  oss << key.id << "_" << suffix << "_" << timestamp;
  stream_mux_cfg->hls_prefix = oss.str();

  return stream_mux_cfg;
}

std::string VideoConfig::GetStreamType(const StreamKey& key) const {
  return (key.kind == StreamKind::kCamera) ? kCameraSuffix : kShareSuffix;
}
