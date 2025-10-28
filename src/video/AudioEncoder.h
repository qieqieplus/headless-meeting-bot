#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

struct AudioEncoderConfig {
  int sampleRate = 32000;    // Target sample rate
  int channels = 1;          // Stereo (1 = mono, 2 = stereo)
  int bitrateKbps = 128;     // AAC bitrate
  std::string codec = "aac"; // "aac" or "libfdk_aac"
};

// FFmpeg-based AAC audio encoder
class AudioEncoder {
public:
  AudioEncoder();
  ~AudioEncoder();

  // Initialize encoder with given config
  bool initialize(const AudioEncoderConfig &config);

  // Reinitialize with new config (closes and reopens encoder)
  bool reinitialize(const AudioEncoderConfig &config);

  // Shutdown encoder
  void shutdown();

  // Encode PCM S16LE audio and return encoded packet
  // Returns AVPacket* on success (caller must call av_packet_unref), nullptr on
  // failure or when no packet ready Input: interleaved S16LE PCM samples;
  // timestampMs used to derive initial PTS alignment
  AVPacket *encodePCM(const uint8_t *pcmData, size_t pcmLength,
                      int inputSampleRate, int inputChannels,
                      int64_t timestampMs);

  int getSampleRate() const { return codecCtx ? codecCtx->sample_rate : 0; }
  int getChannels() const {
    return codecCtx ? codecCtx->ch_layout.nb_channels : 0;
  }
  AVCodecContext *getCodecContext() { return codecCtx; }
  int getFrameSize() const;
  int64_t getFrameDuration() const { return frameDuration; }

private:
  bool openEncoder();
  void closeEncoder();
  const AVCodec *selectCodec(const std::string &codecName);
  bool configureEncoder(AVCodecContext *ctx, const AVCodec *codec);
  bool ensureResampler(int inputSampleRate, int inputChannels);
  int convertToFifo(const uint8_t *pcmData, size_t pcmLength,
                    int inputSampleRate, int inputChannels);

private:
  AudioEncoderConfig currentConfig;
  AVCodecContext *codecCtx = nullptr;
  AVFrame *frame = nullptr;
  AVPacket *pkt = nullptr;
  SwrContext *swrCtx = nullptr;
  AVAudioFifo *audioFifo = nullptr;
  int64_t encodedSamples = 0;
  int64_t basePtsSamples = AV_NOPTS_VALUE;
  int64_t nextPts = 0;
  int64_t lastPts = AV_NOPTS_VALUE;
  int64_t frameDuration = 0;
  int lastInputSampleRate = 0;
  int lastInputChannels = 0;
};
