#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "AudioEncoder.h"
#include "HlsMuxer.h"
#include "VideoEncoder.h"

class MediaEncodePipeline {
public:
  using HlsFileCallback =
      std::function<void(const char *filename, const uint8_t *data, size_t size,
                         int is_playlist, uint64_t sequence)>;

  MediaEncodePipeline();
  ~MediaEncodePipeline();

  // Start HLS encoding pipeline
  bool start(const VideoEncoderConfig &videoEncoderCfg,
             const AudioEncoderConfig &audioEncoderCfg,
             const HlsMuxerConfig &muxerCfg, HlsFileCallback hlsCb);
  void stop();

  // Non-blocking video push; copies planes into internal pool buffer. Drops if
  // overloaded.
  void pushVideoI420(const char *y, const char *u, const char *v,
                     unsigned int width, unsigned int height,
                     unsigned long long timestampMs);

  // Non-blocking audio push; copies PCM data into internal pool buffer. Drops
  // if overloaded.
  void pushAudioPCM(const uint8_t *pcmData, size_t pcmLength,
                    uint32_t sampleRate, uint32_t channels,
                    uint64_t timestampMs);

  void requestIDR();

private:
  struct VideoFrame {
    std::vector<uint8_t> y;
    std::vector<uint8_t> u;
    std::vector<uint8_t> v;
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned long long ts = 0;
    bool occupied = false;
  };

  struct AudioFrame {
    std::vector<uint8_t> data;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    uint64_t ts = 0;
    bool occupied = false;
  };

  void workerLoop();
  bool ensureVideoEncoder(unsigned int w, unsigned int h);
  bool ensureAudioEncoder();

private:
  std::thread worker;
  std::atomic<bool> running{false};

  VideoEncoder videoEncoder;
  AudioEncoder audioEncoder;
  HlsMuxer muxer;
  VideoEncoderConfig videoEncoderCfg;
  AudioEncoderConfig audioEncoderCfg;
  HlsMuxerConfig muxerCfg;
  HlsFileCallback fileCallback;

  // Video queue
  std::mutex videoMtx;
  std::condition_variable videoCv;
  std::vector<VideoFrame> videoPool;
  size_t videoHead = 0;
  size_t videoTail = 0;
  size_t videoSize = 0;

  // Audio queue
  std::mutex audioMtx;
  std::condition_variable audioCv;
  std::vector<AudioFrame> audioPool;
  size_t audioHead = 0;
  size_t audioTail = 0;
  size_t audioSize = 0;
};
