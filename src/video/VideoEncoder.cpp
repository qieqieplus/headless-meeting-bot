#include "VideoEncoder.h"

#include <cstring>

#include "util/Logger.h"

VideoEncoder::VideoEncoder() = default;

VideoEncoder::~VideoEncoder() { Shutdown(); }

bool VideoEncoder::Initialize(const VideoEncoderConfig& config) {
  videoConfig_ = config;
  return OpenEncoder();
}

bool VideoEncoder::Reinitialize(const VideoEncoderConfig& config) {
  Shutdown();
  videoConfig_ = config;
  return OpenEncoder();
}

void VideoEncoder::Shutdown() { CloseEncoder(); }

const AVCodec* VideoEncoder::SelectCodec(const std::string& encoder) {
  const AVCodec* codec = nullptr;

  if (encoder == "nvenc") {
    codec = avcodec_find_encoder_by_name("h264_nvenc");
  }

  if (!codec) {
    codec = avcodec_find_encoder_by_name("libx264");
  }

  if (codec) {
    Logger::GetInstance().Info(std::string("Encoder: ") + codec->name);
  } else {
    Logger::GetInstance().Error("No H.264 encoder available");
  }

  return codec;
}

bool VideoEncoder::ConfigureEncoder(AVCodecContext* ctx, const AVCodec* codec) {
  if (videoConfig_.fps <= 0) {
    constexpr int kDefaultAutoFps = 10;
    videoConfig_.fps = kDefaultAutoFps;
  }

  ctx->width = videoConfig_.width;
  ctx->height = videoConfig_.height;
  ctx->time_base = AVRational{1, 1000000};  // microseconds
  ctx->framerate = AVRational{videoConfig_.fps, 1};
  ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  ctx->bit_rate = videoConfig_.bitrate_kbps * 1000;
  ctx->gop_size = videoConfig_.gop_size;
  ctx->max_b_frames = 0;  // No B-frames for low latency

  bool is_nvenc = (strstr(codec->name, "nvenc") != nullptr);

  if (is_nvenc) {
    // NVENC-specific options
    av_opt_set(ctx->priv_data, "preset", "fast", 0);
    av_opt_set(ctx->priv_data, "rc", "cbr_ld_hq", 0);
    av_opt_set(ctx->priv_data, "zerolatency", "1", 0);
    av_opt_set(ctx->priv_data, "profile", videoConfig_.profile.c_str(), 0);
  } else {
    // libx264 options
    av_opt_set(ctx->priv_data, "preset", videoConfig_.preset.c_str(), 0);
    av_opt_set(ctx->priv_data, "tune", "zerolatency", 0);
    av_opt_set(ctx->priv_data, "profile", videoConfig_.profile.c_str(), 0);
  }

  // Global header for MP4/HLS
  ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  return true;
}

bool VideoEncoder::OpenEncoder() {
  if (videoConfig_.width <= 0 || videoConfig_.height <= 0) {
    Logger::GetInstance().Error("Encoder requires a valid width and height");
    return false;
  }

  const AVCodec* codec = SelectCodec(videoConfig_.encoder);
  if (!codec) {
    return false;
  }

  codecCtx_ = avcodec_alloc_context3(codec);
  if (!codecCtx_) {
    Logger::GetInstance().Error("Failed to allocate codec context");
    return false;
  }

  if (!ConfigureEncoder(codecCtx_, codec)) {
    CloseEncoder();
    return false;
  }

  if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
    Logger::GetInstance().Error("Failed to open codec");
    CloseEncoder();
    return false;
  }

  frame_ = av_frame_alloc();
  if (!frame_) {
    Logger::GetInstance().Error("Failed to allocate frame");
    CloseEncoder();
    return false;
  }

  frame_->format = codecCtx_->pix_fmt;
  frame_->width = codecCtx_->width;
  frame_->height = codecCtx_->height;

  if (av_frame_get_buffer(frame_, 0) < 0) {
    Logger::GetInstance().Error("Failed to allocate frame buffer");
    CloseEncoder();
    return false;
  }

  // Calculate and cache frame duration based on framerate and time_base
  if (codecCtx_->framerate.num > 0 && codecCtx_->framerate.den > 0) {
    AVRational frame_rate = codecCtx_->framerate;
    frameDuration_ = av_rescale_q(1, av_inv_q(frame_rate), codecCtx_->time_base);
  } else {
    frameDuration_ = 0;
  }

  frameCount_ = 0;
  Logger::GetInstance().Success("FFmpeg encoder initialized successfully");
  return true;
}

void VideoEncoder::CloseEncoder() {
  /*
  if (swsCtx) {
      sws_freeContext(swsCtx);
      swsCtx = nullptr;
  }
  */
  // Clear packet queue
  while (!packetQueue_.empty()) {
    AVPacket* pkt = packetQueue_.front();
    packetQueue_.pop();
    av_packet_free(&pkt);
  }

  if (frame_) {
    av_frame_free(&frame_);
    frame_ = nullptr;
  }
  if (codecCtx_) {
    avcodec_free_context(&codecCtx_);
    codecCtx_ = nullptr;
  }
  frameCount_ = 0;
  frameDuration_ = 0;
}

namespace {
// Helper function to copy plane data with optimal strategy
inline void CopyPlaneData(uint8_t* dst, int dst_stride, const uint8_t* src, int src_width,
                          int height) {
  if (dst_stride == src_width) {
    // Fast path: single contiguous copy when strides match
    std::memcpy(dst, src, static_cast<size_t>(src_width) * height);
  } else {
    // Stride mismatch: copy row by row
    for (int y = 0; y < height; ++y) {
      std::memcpy(dst + y * dst_stride, src + y * src_width, src_width);
    }
  }
}
}  // namespace

AVFrame* VideoEncoder::ConvertToAvFrame(const uint8_t* y_plane, const uint8_t* u_plane,
                                        const uint8_t* v_plane, int width, int height,
                                        int64_t pts_us) {
  if (!frame_ || !codecCtx_) {
    return nullptr;
  }

  if (av_frame_make_writable(frame_) < 0) {
    Logger::GetInstance().Error("Failed to make frame writable");
    return nullptr;
  }

  // Copy I420 planes to AVFrame using optimized helper
  CopyPlaneData(frame_->data[0], frame_->linesize[0], y_plane, width, height);

  int chroma_width = width / 2;
  int chroma_height = height / 2;
  CopyPlaneData(frame_->data[1], frame_->linesize[1], u_plane, chroma_width, chroma_height);
  CopyPlaneData(frame_->data[2], frame_->linesize[2], v_plane, chroma_width, chroma_height);

  frame_->pts = pts_us;

  if (forceKeyframe_.exchange(false)) {
    frame_->pict_type = AV_PICTURE_TYPE_I;
    // frame_->flags |= AV_FRAME_FLAG_KEY;
  } else {
    frame_->pict_type = AV_PICTURE_TYPE_NONE;
  }

  return frame_;
}

bool VideoEncoder::EncodeI420(const uint8_t* y_plane, const uint8_t* u_plane,
                              const uint8_t* v_plane, int width, int height, int64_t pts_us) {
  if (!codecCtx_ || !frame_) {
    return false;
  }

  if (width != videoConfig_.width || height != videoConfig_.height) {
    Logger::GetInstance().Error("Frame dimensions don't match encoder config");
    return false;
  }

  AVFrame* input_frame = ConvertToAvFrame(y_plane, u_plane, v_plane, width, height, pts_us);
  if (!input_frame) {
    return false;
  }

  // Send frame to encoder
  int ret = avcodec_send_frame(codecCtx_, input_frame);
  if (ret < 0) {
    Logger::GetInstance().Error("Error sending frame to encoder");
    return false;
  }

  // Drain all available packets from encoder
  // Note: The encoder may buffer frames, so we might get 0, 1, or multiple
  // packets
  DrainPackets();
  return true;
}

void VideoEncoder::DrainPackets() {
  if (!codecCtx_) {
    return;
  }

  while (true) {
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
      Logger::GetInstance().Error("Failed to allocate packet");
      break;
    }

    int ret = avcodec_receive_packet(codecCtx_, pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      // No more packets available right now (EAGAIN) or encoder flushed (EOF)
      av_packet_free(&pkt);
      break;
    } else if (ret < 0) {
      Logger::GetInstance().Error("Error receiving packet from encoder");
      av_packet_free(&pkt);
      break;
    }

    // Successfully received a packet - add to queue
    packetQueue_.push(pkt);
    frameCount_++;
  }
}

AVPacket* VideoEncoder::GetNextPacket() {
  if (packetQueue_.empty()) {
    return nullptr;
  }

  AVPacket* pkt = packetQueue_.front();
  packetQueue_.pop();
  return pkt;  // Caller must call av_packet_free()
}

void VideoEncoder::Flush() {
  if (!codecCtx_) {
    return;
  }

  // Send NULL frame to signal end of stream
  int ret = avcodec_send_frame(codecCtx_, nullptr);
  if (ret < 0 && ret != AVERROR_EOF) {
    Logger::GetInstance().Error("Error flushing encoder");
    return;
  }

  // Drain all remaining packets
  DrainPackets();
}

void VideoEncoder::RequestIdr() { forceKeyframe_.store(true); }
