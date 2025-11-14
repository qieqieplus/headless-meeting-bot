#include "MediaConfig.h"

#include <chrono>
#include <iomanip>
#include <sstream>

#include "video/EncoderConfig.h"

MediaConfig::MediaConfig(std::string meeting_id) : meeting_id_(std::move(meeting_id)) {}

void MediaConfig::SetAudioCallback(AudioCallback cb) {
  std::lock_guard<std::mutex> lock(mtx_);
  audio_callback_ = std::move(cb);
}

void MediaConfig::ClearAudioCallback() {
  std::lock_guard<std::mutex> lock(mtx_);
  audio_callback_ = nullptr;
}

void MediaConfig::SetHlsMediaCallback(const VideoEncoderConfig& v, const AudioEncoderConfig& a,
                                      const HlsMuxerConfig& m, HlsFileCallback cb) {
  std::lock_guard<std::mutex> lock(mtx_);
  video_cfg_ = std::make_unique<VideoEncoderConfig>(v);
  audio_cfg_ = std::make_unique<AudioEncoderConfig>(a);
  muxer_cfg_ = std::make_unique<HlsMuxerConfig>(m);
  hls_file_callback_ = std::move(cb);
}

void MediaConfig::ClearHlsMediaParams() {
  std::lock_guard<std::mutex> lock(mtx_);
  video_cfg_.reset();
  audio_cfg_.reset();
  muxer_cfg_.reset();
  hls_file_callback_ = nullptr;
}

MediaConfig::AudioCallback MediaConfig::GetAudioCallback() const {
  std::lock_guard<std::mutex> lock(mtx_);
  return audio_callback_;
}

std::unique_ptr<AudioEncoderConfig> MediaConfig::GetAudioEncoderConfig() const {
  std::lock_guard<std::mutex> lock(mtx_);
  if (!audio_cfg_) {
    return nullptr;
  }
  return std::make_unique<AudioEncoderConfig>(*audio_cfg_);
}

MediaConfig::HlsFileCallback MediaConfig::GetHlsFileCallback() const {
  std::lock_guard<std::mutex> lock(mtx_);
  return hls_file_callback_;
}

std::unique_ptr<VideoEncoderConfig> MediaConfig::GetVideoEncoderConfigForStream(
    const StreamKey& key) const {
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

std::unique_ptr<HlsMuxerConfig> MediaConfig::GetMuxerConfigForStream(const StreamKey& key) const {
  std::lock_guard<std::mutex> lock(mtx_);
  if (!muxer_cfg_) {
    return nullptr;
  }

  using clock = std::chrono::system_clock;
  auto timestamp = clock::to_time_t(clock::now());

  auto stream_mux_cfg = std::make_unique<HlsMuxerConfig>(*muxer_cfg_);
  const std::string suffix = GetStreamSuffix(key);
  stream_mux_cfg->hls_prefix = muxer_cfg_->hls_prefix + suffix + "-" + std::to_string(timestamp);

  return stream_mux_cfg;
}

std::string MediaConfig::GetStreamSuffix(const StreamKey& key) const {
  return (key.kind == StreamKind::kCamera)
             ? std::string(kCameraSuffix) + meeting_id_ + "-" + std::to_string(key.id)
             : std::string(kShareSuffix) + meeting_id_ + "-" + std::to_string(key.id);
}
