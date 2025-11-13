#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "AvioMemorySink.h"
#include "MediaConfig.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

// HLS muxer with fMP4 segments
class HlsMuxer {
 public:
  HlsMuxer();
  ~HlsMuxer();

  // Initialize muxer with config and file callback
  bool Initialize(const HlsMuxerConfig& config, HlsFileCallback file_callback);

  // Start muxing (writes header and init segment)
  bool Start();

  // Write an encoded video packet to the muxer
  bool WriteVideoPacket(AVPacket* pkt);

  // Write an encoded audio packet to the muxer
  bool WriteAudioPacket(AVPacket* pkt);

  // Write a packet (deprecated - use writeVideoPacket or writeAudioPacket)
  bool WritePacket(AVPacket* pkt) { return WriteVideoPacket(pkt); }

  // Finalize muxing (writes trailer and final playlist with EXT-X-ENDLIST)
  void Finalize();

  // Shutdown muxer
  void Shutdown();

  AVCodecParameters* GetVideoCodecParams();
  AVCodecParameters* GetAudioCodecParams();
  bool ConfigureAudioStream(const AVCodecContext* audio_ctx);

  // Check if muxer is ready to accept packets (header has been written)
  bool IsReady() const { return headerWritten_; }

 private:
  bool OpenMuxer();
  void CloseMuxer();

 private:
  HlsMuxerConfig muxerConfig_;
  AVFormatContext* fmtCtx_ = nullptr;
  AVStream* videoStream_ = nullptr;
  AVStream* audioStream_ = nullptr;
  AvioMemorySink avioSink_;
  bool headerWritten_ = false;
  AVRational videoCodecTimeBase_ = {0, 0};  // Time base from encoder
  AVRational audioCodecTimeBase_ = {0, 0};  // Time base from encoder
};
