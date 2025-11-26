#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "AudioEncoder.h"

class AudioEncodePipeline {
 public:
  using EncodedAudioCallback =
      std::function<void(const uint8_t* encoded_data, size_t size, uint64_t timestamp_ms)>;

  AudioEncodePipeline();
  ~AudioEncodePipeline();

  // Start encoding pipeline
  bool Start(const AudioEncoderConfig& config, EncodedAudioCallback callback);
  void Stop();

  // Non-blocking audio push; copies PCM data into internal pool buffer. Drops if overloaded.
  void PushAudioPcm(const uint8_t* pcm_data, size_t pcm_length, uint32_t sample_rate,
                    uint32_t channels, uint64_t timestamp_ms);

 private:
  struct AudioFrame {
    std::vector<uint8_t> data;
    uint32_t sample_rate = 0;
    uint32_t channels = 0;
    uint64_t ts = 0;
    bool occupied = false;
  };

  void WorkerLoop();

 private:
  std::thread worker_;
  std::atomic<bool> running_{false};

  AudioEncoder audioEncoder_;
  AudioEncoderConfig audioEncoderCfg_;
  EncodedAudioCallback encodedCallback_;

  // Audio queue
  std::mutex queueMtx_;
  std::condition_variable queueCv_;
  std::vector<AudioFrame> audioPool_;
  size_t audioHead_ = 0;
  size_t audioTail_ = 0;
  size_t audioSize_ = 0;
};
