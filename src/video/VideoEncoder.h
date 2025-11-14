#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <queue>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
/* #include <libswscale/swscale.h> */
}

#include "EncoderConfig.h"

// FFmpeg-based H.264 encoder with libx264 and h264_nvenc support
class VideoEncoder {
 public:
  VideoEncoder();
  ~VideoEncoder();

  // Initialize encoder with given config
  bool Initialize(const VideoEncoderConfig& config);

  // Reinitialize with new config (closes and reopens encoder)
  bool Reinitialize(const VideoEncoderConfig& config);

  // Shutdown encoder
  void Shutdown();

  // Encode one I420 frame. Returns true on success, false on error.
  // Note: Encoding a single frame may produce 0, 1, or multiple packets due to
  // encoder buffering. Use getNextPacket() to retrieve ready packets.
  // Input planes must be contiguous with standard strides: Y=width, U=V=width/2
  bool EncodeI420(const uint8_t* y_plane, const uint8_t* u_plane, const uint8_t* v_plane, int width,
                  int height, int64_t pts_us);

  // Get next available encoded packet from the internal queue.
  // Returns AVPacket* on success (caller must call av_packet_free), nullptr if
  // no packets are ready. Call this in a loop after encodeI420() until it
  // returns nullptr.
  AVPacket* GetNextPacket();

  // Flush the encoder to retrieve all remaining buffered packets.
  // Call getNextPacket() after this to retrieve flushed packets.
  void Flush();

  // Request next frame to be a keyframe (IDR)
  void RequestIdr();

  int GetWidth() const { return videoConfig_.width; }
  int GetHeight() const { return videoConfig_.height; }
  int GetFps() const { return videoConfig_.fps; }
  AVCodecContext* GetCodecContext() { return codecCtx_; }
  int64_t GetFrameDuration() const { return frameDuration_; }

 private:
  bool OpenEncoder();
  void CloseEncoder();
  const AVCodec* SelectCodec(const std::string& encoder_name);
  bool ConfigureEncoder(AVCodecContext* ctx, const AVCodec* codec);
  AVFrame* ConvertToAvFrame(const uint8_t* y_plane, const uint8_t* u_plane, const uint8_t* v_plane,
                            int width, int height, int64_t pts_us);
  void DrainPackets();

 private:
  VideoEncoderConfig videoConfig_;
  AVCodecContext* codecCtx_ = nullptr;
  AVFrame* frame_ = nullptr;
  // SwsContext* swsCtx = nullptr;
  int64_t frameCount_ = 0;
  std::atomic<bool> forceKeyframe_{false};
  int64_t frameDuration_ = 0;
  std::queue<AVPacket*> packetQueue_;
};
