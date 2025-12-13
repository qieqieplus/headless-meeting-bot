#include "AudioEncodePipeline.h"

#include <sstream>

#include "util/Logger.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

constexpr size_t kAudioPoolSize = 16;

AudioEncodePipeline::AudioEncodePipeline() { audioPool_.resize(kAudioPoolSize); }

AudioEncodePipeline::~AudioEncodePipeline() { Stop(); }

bool AudioEncodePipeline::Start(const AudioEncoderConfig& config, EncodedAudioCallback callback) {
  if (running_.load(std::memory_order_acquire)) {
    Logger::GetInstance().Warn("AudioEncodePipeline already running");
    return false;
  }

  audioEncoderCfg_ = config;
  encodedCallback_ = std::move(callback);

  if (!audioEncoder_.Initialize(audioEncoderCfg_)) {
    Logger::GetInstance().Error("Failed to initialize audio encoder");
    return false;
  }

  running_.store(true, std::memory_order_release);
  worker_ = std::thread(&AudioEncodePipeline::WorkerLoop, this);

  Logger::GetInstance().Success("AudioEncodePipeline started");
  return true;
}

void AudioEncodePipeline::Stop() {
  if (!running_.load(std::memory_order_acquire)) {
    return;
  }

  running_.store(false, std::memory_order_release);
  queueCv_.notify_all();

  if (worker_.joinable()) {
    worker_.join();
  }

  audioEncoder_.Shutdown();
  Logger::GetInstance().Success("AudioEncodePipeline stopped");
}

void AudioEncodePipeline::PushAudioPcm(const uint8_t* pcm_data, size_t pcm_length,
                                       uint32_t sample_rate, uint32_t channels,
                                       uint64_t timestamp_ms) {
  if (!running_.load(std::memory_order_acquire)) {
    return;
  }

  std::lock_guard<std::mutex> lock(queueMtx_);

  // Check if pool is full
  if (audioSize_ >= audioPool_.size()) {
    Logger::GetInstance().Warn("Audio pool full, dropping frame");
    return;
  }

  // Get next available slot
  auto& frame = audioPool_[audioTail_];
  if (frame.occupied) {
    Logger::GetInstance().Error("Audio pool corruption detected");
    return;
  }

  // Copy data
  frame.data.assign(pcm_data, pcm_data + pcm_length);
  frame.sample_rate = sample_rate;
  frame.channels = channels;
  frame.ts = timestamp_ms;
  frame.occupied = true;

  audioTail_ = (audioTail_ + 1) % audioPool_.size();
  audioSize_++;

  queueCv_.notify_one();
}

void AudioEncodePipeline::WorkerLoop() {
  Logger::GetInstance().Info("AudioEncodePipeline worker started");

  while (running_.load(std::memory_order_acquire)) {
    AudioFrame frame;
    bool hasFrame = false;

    // Wait for frame
    {
      std::unique_lock<std::mutex> lock(queueMtx_);
      queueCv_.wait(lock,
                    [this] { return !running_.load(std::memory_order_acquire) || audioSize_ > 0; });

      if (!running_.load(std::memory_order_acquire)) {
        break;
      }

      if (audioSize_ > 0) {
        auto& poolFrame = audioPool_[audioHead_];
        if (!poolFrame.occupied) {
          Logger::GetInstance().Error("Audio pool corruption in worker");
          continue;
        }

        frame = std::move(poolFrame);
        poolFrame.occupied = false;
        audioHead_ = (audioHead_ + 1) % audioPool_.size();
        audioSize_--;
        hasFrame = true;
      }
    }

    if (!hasFrame) {
      continue;
    }

    // Encode the frame
    if (!audioEncoder_.EncodePcm(frame.data.data(), frame.data.size(), frame.sample_rate,
                                 frame.channels, frame.ts)) {
      Logger::GetInstance().Error("Failed to encode audio frame");
      continue;
    }

    // Retrieve all available encoded packets
    while (AVPacket* pkt = audioEncoder_.GetNextPacket()) {
      if (encodedCallback_ && pkt->size > 0) {
        encodedCallback_(pkt->data, pkt->size, frame.ts);
      }
      av_packet_free(&pkt);
    }
  }

  // Flush encoder and retrieve remaining packets
  audioEncoder_.Flush();
  while (AVPacket* pkt = audioEncoder_.GetNextPacket()) {
    if (encodedCallback_ && pkt->size > 0) {
      encodedCallback_(pkt->data, pkt->size, 0);
    }
    av_packet_free(&pkt);
  }

  Logger::GetInstance().Info("AudioEncodePipeline worker stopped");
}
