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
  using HlsFileCallback = std::function<void(const char* filename, const uint8_t* data, size_t size,
                                             int is_playlist, uint64_t sequence)>;

  MediaEncodePipeline();
  ~MediaEncodePipeline();

  // Start HLS encoding pipeline
  bool Start(const VideoEncoderConfig& video_encoder_cfg,
             const AudioEncoderConfig& audio_encoder_cfg, const HlsMuxerConfig& muxer_cfg,
             HlsFileCallback hls_cb);
  void Stop();

  // Non-blocking video push; copies planes into internal pool buffer. Drops if
  // overloaded.
  void PushVideoI420(const char* y, const char* u, const char* v, unsigned int width,
                     unsigned int height, uint64_t timestamp_ms);

  // Non-blocking audio push; copies PCM data into internal pool buffer. Drops
  // if overloaded.
  void PushAudioPcm(const uint8_t* pcm_data, size_t pcm_length, uint32_t sample_rate,
                    uint32_t channels, uint64_t timestamp_ms);

  void RequestIdr();

 private:
  struct VideoFrame {
    std::vector<uint8_t> y;
    std::vector<uint8_t> u;
    std::vector<uint8_t> v;
    unsigned int width = 0;
    unsigned int height = 0;
    uint64_t ts = 0;
    bool occupied = false;
  };

  struct AudioFrame {
    std::vector<uint8_t> data;
    uint32_t sample_rate = 0;
    uint32_t channels = 0;
    uint64_t ts = 0;
    bool occupied = false;
  };

  void WorkerLoop();
  bool EnsureVideoEncoder(unsigned int w, unsigned int h);
  bool EnsureAudioEncoder();
  void DrainQueues();

 private:
  std::thread worker_;
  std::atomic<bool> running_{false};

  VideoEncoder videoEncoder_;
  AudioEncoder audioEncoder_;
  HlsMuxer muxer_;
  VideoEncoderConfig videoEncoderCfg_;
  AudioEncoderConfig audioEncoderCfg_;
  HlsMuxerConfig muxerCfg_;
  HlsFileCallback fileCallback_;

  // Video queue
  std::mutex queueMtx_;
  std::condition_variable queueCv_;
  std::vector<VideoFrame> videoPool_;
  size_t videoHead_ = 0;
  size_t videoTail_ = 0;
  size_t videoSize_ = 0;

  // Audio queue
  std::vector<AudioFrame> audioPool_;
  size_t audioHead_ = 0;
  size_t audioTail_ = 0;
  size_t audioSize_ = 0;
};
