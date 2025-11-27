#include "MediaEncodePipeline.h"

#include <algorithm>
#include <cstring>

#include "util/Logger.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace {
// Constants
const int kDefaultVideoPoolSize = 8;
const int kDefaultAudioPoolSize = 128;

// Helper function to drain all packets from video encoder and write to muxer
inline void DrainAndWriteVideoPackets(VideoEncoder& encoder, HlsMuxer& muxer) {
  AVPacket* pkt;
  while ((pkt = encoder.GetNextPacket()) != nullptr) {
    if (pkt->duration == 0) {
      pkt->duration = encoder.GetFrameDuration();
    }
    bool written = muxer.WriteVideoPacket(pkt);
    av_packet_free(&pkt);

    if (!written) {
      Logger::GetInstance().Error("Failed to write video packet to HLS muxer");
    }
  }
}

// Helper function to drain all packets from audio encoder and write to muxer
inline void DrainAndWriteAudioPackets(AudioEncoder& encoder, HlsMuxer& muxer) {
  AVPacket* pkt;
  while ((pkt = encoder.GetNextPacket()) != nullptr) {
    if (pkt->duration == 0) {
      pkt->duration = encoder.GetFrameDuration();
    }
    bool written = muxer.WriteAudioPacket(pkt);
    av_packet_free(&pkt);

    if (!written) {
      Logger::GetInstance().Error("Failed to write audio packet to HLS muxer");
    }
  }
}
}  // namespace

MediaEncodePipeline::MediaEncodePipeline() = default;
MediaEncodePipeline::~MediaEncodePipeline() { Stop(); }

bool MediaEncodePipeline::Start(const VideoEncoderConfig& vid_enc_cfg,
                                const AudioEncoderConfig& aud_enc_cfg,
                                const HlsMuxerConfig& mux_cfg, HlsFileCallback hls_cb) {
  Stop();
  videoEncoderCfg_ = vid_enc_cfg;
  audioEncoderCfg_ = aud_enc_cfg;
  muxerCfg_ = mux_cfg;
  fileCallback_ = std::move(hls_cb);

  // Initialize video pool
  videoPool_.clear();
  videoPool_.resize(kDefaultVideoPoolSize);
  videoHead_ = videoTail_ = videoSize_ = 0;

  // Initialize audio pool
  audioPool_.clear();
  audioPool_.resize(kDefaultAudioPoolSize);
  audioHead_ = audioTail_ = audioSize_ = 0;

  running_ = true;
  worker_ = std::thread(&::MediaEncodePipeline::WorkerLoop, this);
  return true;
}

void MediaEncodePipeline::Stop() {
  if (running_.exchange(false)) {
    queueCv_.notify_all();
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  // Process any remaining frames in the queues that the worker didn't get to
  DrainQueues();

  // Flush encoders to get any remaining buffered packets
  if (muxer_.IsReady()) {
    // Flush video encoder
    videoEncoder_.Flush();
    DrainAndWriteVideoPackets(videoEncoder_, muxer_);

    // Flush audio encoder
    audioEncoder_.Flush();
    DrainAndWriteAudioPackets(audioEncoder_, muxer_);
  }

  muxer_.Shutdown();
  videoEncoder_.Shutdown();
  audioEncoder_.Shutdown();
}

void MediaEncodePipeline::DrainQueues() {
  std::unique_lock<std::mutex> Lock(queueMtx_);

  // Drain video queue
  while (videoSize_ > 0) {
    VideoFrame& vf = videoPool_[videoTail_];

    if (muxer_.IsReady() && muxer_.GetVideoCodecParams() != nullptr) {
      if (EnsureVideoEncoder(vf.width, vf.height)) {
        int64_t pts_us = static_cast<int64_t>(vf.ts) * 1000;
        bool success = videoEncoder_.EncodeI420(vf.y.data(), vf.u.data(), vf.v.data(), vf.width,
                                                vf.height, pts_us);
        if (success) {
          DrainAndWriteVideoPackets(videoEncoder_, muxer_);
        }
      }
    }

    // Clear frame and advance tail
    vf = VideoFrame{};
    videoTail_ = (videoTail_ + 1) % videoPool_.size();
    --videoSize_;
  }

  // Drain audio queue
  while (audioSize_ > 0) {
    AudioFrame& af = audioPool_[audioTail_];

    if (muxer_.IsReady() && muxer_.GetAudioCodecParams() != nullptr) {
      if (EnsureAudioEncoder()) {
        bool success = audioEncoder_.EncodePcm(af.data.data(), af.data.size(), af.sample_rate,
                                               af.channels, af.ts);
        if (success) {
          DrainAndWriteAudioPackets(audioEncoder_, muxer_);
        }
      }
    }

    // Clear frame and advance tail
    af = AudioFrame{};
    audioTail_ = (audioTail_ + 1) % audioPool_.size();
    --audioSize_;
  }
}

void MediaEncodePipeline::PushVideoI420(const char* y, const char* u, const char* v,
                                        unsigned int width, unsigned int height,
                                        uint64_t timestamp_ms) {
  if (!running_) {
    return;
  }
  std::unique_lock<std::mutex> Lock(queueMtx_);
  if (videoSize_ == videoPool_.size()) {
    Logger::GetInstance().Warn("Video queue full: dropping frame");
    return;
  }
  VideoFrame& f = videoPool_[videoHead_];
  f.width = width;
  f.height = height;
  f.ts = timestamp_ms;
  size_t y_size = static_cast<size_t>(width) * height;
  size_t c_w = width / 2;
  size_t c_h = height / 2;
  size_t c_size = c_w * c_h;
  f.y.resize(y_size);
  f.u.resize(c_size);
  f.v.resize(c_size);
  std::memcpy(f.y.data(), y, y_size);
  std::memcpy(f.u.data(), u, c_size);
  std::memcpy(f.v.data(), v, c_size);
  f.occupied = true;
  videoHead_ = (videoHead_ + 1) % videoPool_.size();
  ++videoSize_;
  Lock.unlock();
  queueCv_.notify_one();
}

void MediaEncodePipeline::PushAudioPcm(const uint8_t* pcm_data, size_t pcm_length,
                                       uint32_t sample_rate, uint32_t channels,
                                       uint64_t timestamp_ms) {
  if (!running_) {
    return;
  }
  // Avoid building backlog before the muxer header is written; it's safe to
  // drop initial audio frames to keep A/V in sync from the start.
  if (!muxer_.IsReady()) {
    return;
  }
  std::unique_lock<std::mutex> Lock(queueMtx_);
  if (audioSize_ == audioPool_.size()) {
    Logger::GetInstance().Warn("Audio queue full: dropping frame");
    return;
  }
  AudioFrame& f = audioPool_[audioHead_];
  f.data.resize(pcm_length);
  std::memcpy(f.data.data(), pcm_data, pcm_length);
  f.sample_rate = sample_rate;
  f.channels = channels;
  f.ts = timestamp_ms;
  f.occupied = true;
  audioHead_ = (audioHead_ + 1) % audioPool_.size();
  ++audioSize_;
  Lock.unlock();
  queueCv_.notify_one();
}

void MediaEncodePipeline::RequestIdr() { videoEncoder_.RequestIdr(); }

bool MediaEncodePipeline::EnsureVideoEncoder(unsigned int w, unsigned int h) {
  if (videoEncoder_.GetWidth() == static_cast<int>(w) &&
      videoEncoder_.GetHeight() == static_cast<int>(h)) {
    return true;
  }

  // First-time initialization: strict ordering required by FFmpeg/libav
  // 1. Video encoder (determines frame format/size)
  // 2. Muxer (needs video codec params for container header)
  // 3. Audio encoder (can init independently)
  // 4. Configure audio stream in muxer (needs audio codec params)
  // 5. Start muxer (writes container header)
  // 6. Flush queues (clear stale frames before first encode)
  if (videoEncoder_.GetWidth() == 0) {
    VideoEncoderConfig cfg = videoEncoderCfg_;
    cfg.width = static_cast<int>(w);
    cfg.height = static_cast<int>(h);

    if (!videoEncoder_.Initialize(cfg)) {
      return false;
    }

    if (!muxer_.Initialize(muxerCfg_, fileCallback_)) {
      return false;
    }

    // Copy video codec parameters from encoder to muxer stream
    AVCodecParameters* video_codec_params = muxer_.GetVideoCodecParams();
    AVCodecContext* video_codec_ctx = videoEncoder_.GetCodecContext();
    if (video_codec_params && video_codec_ctx) {
      avcodec_parameters_from_context(video_codec_params, video_codec_ctx);
      video_codec_params->format = video_codec_ctx->pix_fmt;
    }

    // Initialize audio encoder (if not already initialized)
    if (!EnsureAudioEncoder()) {
      Logger::GetInstance().Warn("Failed to initialize audio encoder");
    }

    // Configure audio stream now that muxer is ready
    if (audioEncoder_.GetSampleRate() > 0) {
      if (!muxer_.ConfigureAudioStream(audioEncoder_.GetCodecContext())) {
        Logger::GetInstance().Error("Failed to configure audio stream");
        return false;
      }
    }

    if (!muxer_.Start()) {
      return false;
    }

    // Flush any pre-start frames to avoid A/V desynchronization
    {
      std::unique_lock<std::mutex> Lock(queueMtx_);
      videoHead_ = videoTail_ = videoSize_ = 0;
      audioHead_ = audioTail_ = audioSize_ = 0;
    }

    return true;
  }

  // Reinitialize if dimensions changed
  VideoEncoderConfig cfg = videoEncoderCfg_;
  cfg.width = static_cast<int>(w);
  cfg.height = static_cast<int>(h);
  return videoEncoder_.Reinitialize(cfg);
}

bool MediaEncodePipeline::EnsureAudioEncoder() {
  if (audioEncoder_.GetSampleRate() > 0) {
    return true;  // Already initialized
  }

  if (!audioEncoder_.Initialize(audioEncoderCfg_)) {
    return false;
  }

  // Only configure audio stream if muxer is ready (has been initialized)
  // This prevents configuring audio stream before video encoder sets up the
  // muxer
  if (muxer_.GetAudioCodecParams() != nullptr) {
    if (!muxer_.ConfigureAudioStream(audioEncoder_.GetCodecContext())) {
      Logger::GetInstance().Error("Failed to configure audio stream in muxer");
      return false;
    }
  }

  return true;
}

void MediaEncodePipeline::WorkerLoop() {
  while (running_) {
    bool has_video = false;
    bool has_audio = false;
    VideoFrame vf;
    AudioFrame af;

    // Efficient wait until there is data in either queue (or shutdown)
    {
      std::unique_lock<std::mutex> Lock(queueMtx_);
      queueCv_.wait(Lock, [&] { return !running_ || videoSize_ > 0 || audioSize_ > 0; });
      if (!running_) {
        break;
      }
      if (audioSize_ > 0) {
        AudioFrame& slot = audioPool_[audioTail_];
        af = std::move(slot);
        slot = AudioFrame{};
        audioTail_ = (audioTail_ + 1) % audioPool_.size();
        --audioSize_;
        has_audio = true;
      }
      if (videoSize_ > 0) {
        VideoFrame& slot = videoPool_[videoTail_];
        vf = std::move(slot);
        slot = VideoFrame{};
        videoTail_ = (videoTail_ + 1) % videoPool_.size();
        --videoSize_;
        has_video = true;
      }
    }

    // Process audio frame
    if (has_audio) {
      if (!EnsureAudioEncoder()) {
        Logger::GetInstance().Error("HLS audio encoder initialization failed");
      } else if (muxer_.IsReady() && muxer_.GetAudioCodecParams() != nullptr) {
        // Pass timestamp in milliseconds (audio encoder expects ms, not us)
        bool success = audioEncoder_.EncodePcm(af.data.data(), af.data.size(), af.sample_rate,
                                               af.channels, af.ts);
        if (success) {
          // Drain all available packets (may be 0, 1, or multiple)
          DrainAndWriteAudioPackets(audioEncoder_, muxer_);
        }
      }
    }

    // Process video frame
    if (has_video) {
      if (!EnsureVideoEncoder(vf.width, vf.height)) {
        Logger::GetInstance().Error("HLS video encoder initialization failed");
      } else {
        // Only encode video if muxer is ready (header written)
        if (muxer_.IsReady() && muxer_.GetVideoCodecParams() != nullptr) {
          // Encode frame (timestamp in microseconds)
          int64_t pts_us = static_cast<int64_t>(vf.ts) * 1000;
          bool success = videoEncoder_.EncodeI420(vf.y.data(), vf.u.data(), vf.v.data(), vf.width,
                                                  vf.height, pts_us);
          if (success) {
            // Drain all available packets (may be 0, 1, or multiple)
            DrainAndWriteVideoPackets(videoEncoder_, muxer_);
          }
        }
      }
    }

    // No polling or biased waits; loop immediately
  }

  Logger::GetInstance().Info("MediaEncodePipeline worker loop exited");
}
