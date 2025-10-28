#pragma once

#include "AvioMemorySink.h"
#include <cstdint>
#include <memory>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

struct HlsMuxerConfig {
  int width = 0;  // auto-detect
  int height = 0; // auto-detect
  int fps = 30;
  int segmentSeconds = 30;
  std::string hlsPrefix = "media";  // Base name for playlist and segments
  std::string playlistType = "vod"; // "vod", "live", or "event"
};

// HLS muxer with fMP4 segments
class HlsMuxer {
public:
  HlsMuxer();
  ~HlsMuxer();

  // Initialize muxer with config and file callback
  bool initialize(const HlsMuxerConfig &config, HlsFileCallback fileCallback);

  // Start muxing (writes header and init segment)
  bool start();

  // Write an encoded video packet to the muxer
  bool writeVideoPacket(AVPacket *pkt);

  // Write an encoded audio packet to the muxer
  bool writeAudioPacket(AVPacket *pkt);

  // Write a packet (deprecated - use writeVideoPacket or writeAudioPacket)
  bool writePacket(AVPacket *pkt) { return writeVideoPacket(pkt); }

  // Finalize muxing (writes trailer and final playlist with EXT-X-ENDLIST)
  void finalize();

  // Shutdown muxer
  void shutdown();

  AVCodecParameters *getVideoCodecParams();
  AVCodecParameters *getAudioCodecParams();
  bool configureAudioStream(const AVCodecContext *audioCtx);

private:
  bool openMuxer();
  void closeMuxer();

private:
  HlsMuxerConfig currentConfig;
  AVFormatContext *fmtCtx = nullptr;
  AVStream *videoStream = nullptr;
  AVStream *audioStream = nullptr;
  AvioMemorySink avioSink;
  bool headerWritten = false;
  AVRational videoCodecTimeBase = {0, 0}; // Time base from encoder
  AVRational audioCodecTimeBase = {0, 0}; // Time base from encoder
};
