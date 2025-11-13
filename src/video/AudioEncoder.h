#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <queue>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include "MediaConfig.h"

// FFmpeg-based AAC audio encoder
class AudioEncoder {
 public:
  AudioEncoder();
  ~AudioEncoder();

  // Initialize encoder with given config
  bool Initialize(const AudioEncoderConfig& config);

  // Reinitialize with new config (closes and reopens encoder)
  bool Reinitialize(const AudioEncoderConfig& config);

  // Shutdown encoder
  void Shutdown();

  // Encode PCM S16LE audio. Returns true on success, false on error.
  // Note: Encoding audio may produce 0, 1, or multiple packets due to
  // encoder buffering and FIFO management. Use getNextPacket() to retrieve
  // ready packets. Input: interleaved S16LE PCM samples; timestampMs used to
  // derive initial PTS alignment
  bool EncodePcm(const uint8_t* pcm_data, size_t pcm_length, int input_sample_rate,
                 int input_channels, int64_t timestamp_ms);

  // Get next available encoded packet from the internal queue.
  // Returns AVPacket* on success (caller must call av_packet_free), nullptr if
  // no packets are ready. Call this in a loop after encodePCM() until it
  // returns nullptr.
  AVPacket* GetNextPacket();

  // Flush the encoder to retrieve all remaining buffered packets.
  // Call getNextPacket() after this to retrieve flushed packets.
  void Flush();

  int GetSampleRate() const { return codecCtx_ ? codecCtx_->sample_rate : 0; }
  int GetChannels() const { return codecCtx_ ? codecCtx_->ch_layout.nb_channels : 0; }
  AVCodecContext* GetCodecContext() { return codecCtx_; }
  int GetFrameSize() const;
  int64_t GetFrameDuration() const { return frameDuration_; }

 private:
  bool OpenEncoder();
  void CloseEncoder();
  const AVCodec* SelectCodec(const std::string& codec_name);
  bool ConfigureEncoder(AVCodecContext* ctx, const AVCodec* codec);
  bool EnsureResampler(int input_sample_rate, int input_channels);
  int ConvertToFifo(const uint8_t* pcm_data, size_t pcm_length, int input_sample_rate,
                    int input_channels);
  void DrainPackets();

 private:
  AudioEncoderConfig audioConfig_;
  AVCodecContext* codecCtx_ = nullptr;
  AVFrame* frame_ = nullptr;
  SwrContext* swrCtx_ = nullptr;
  AVAudioFifo* audioFifo_ = nullptr;
  int64_t nextPts_ = AV_NOPTS_VALUE;  // Next PTS in sample units (encoder's native timebase)
  int64_t frameDuration_ = 0;
  int lastInputSampleRate_ = 0;
  int lastInputChannels_ = 0;
  std::queue<AVPacket*> packetQueue_;
};
