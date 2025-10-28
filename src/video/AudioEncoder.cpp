#include "AudioEncoder.h"
#include "util/Logger.h"

AudioEncoder::AudioEncoder() = default;

AudioEncoder::~AudioEncoder() { shutdown(); }

bool AudioEncoder::initialize(const AudioEncoderConfig &config) {
  currentConfig = config;
  return openEncoder();
}

bool AudioEncoder::reinitialize(const AudioEncoderConfig &config) {
  shutdown();
  currentConfig = config;
  return openEncoder();
}

void AudioEncoder::shutdown() { closeEncoder(); }

const AVCodec *AudioEncoder::selectCodec(const std::string &codecName) {
  const AVCodec *codec = nullptr;

  if (codecName == "libfdk_aac") {
    codec = avcodec_find_encoder_by_name("libfdk_aac");
  }

  if (!codec) {
    codec = avcodec_find_encoder_by_name("aac");
  }

  if (codec) {
    Logger::getInstance().info(std::string("Audio Encoder: ") + codec->name);
  } else {
    Logger::getInstance().error("No AAC encoder available");
  }

  return codec;
}

bool AudioEncoder::configureEncoder(AVCodecContext *ctx, const AVCodec *codec) {
  ctx->sample_fmt = AV_SAMPLE_FMT_FLTP; // AAC typically uses planar float
  ctx->sample_rate = currentConfig.sampleRate;
  ctx->bit_rate = currentConfig.bitrateKbps * 1000;
  ctx->time_base = AVRational{1, currentConfig.sampleRate};

  // Set channel layout
  if (currentConfig.channels == 1) {
    ctx->ch_layout = AV_CHANNEL_LAYOUT_MONO;
  } else if (currentConfig.channels == 2) {
    ctx->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
  } else {
    Logger::getInstance().error("Unsupported channel count: " +
                                std::to_string(currentConfig.channels));
    return false;
  }

  ctx->profile = FF_PROFILE_AAC_LOW;
  ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  return true;
}

bool AudioEncoder::openEncoder() {
  if (currentConfig.sampleRate <= 0 || currentConfig.channels <= 0) {
    Logger::getInstance().error("Invalid audio encoder config");
    return false;
  }

  const AVCodec *codec = selectCodec(currentConfig.codec);
  if (!codec)
    return false;

  codecCtx = avcodec_alloc_context3(codec);
  if (!codecCtx) {
    Logger::getInstance().error("Failed to allocate audio codec context");
    return false;
  }

  if (!configureEncoder(codecCtx, codec)) {
    closeEncoder();
    return false;
  }

  if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
    Logger::getInstance().error("Failed to open audio codec");
    closeEncoder();
    return false;
  }

  Logger::getInstance().info(
      "Audio codec opened: " + std::to_string(codecCtx->sample_rate) + "Hz, " +
      std::to_string(codecCtx->ch_layout.nb_channels) + " channels, " +
      "frame_size=" + std::to_string(codecCtx->frame_size));

  frame = av_frame_alloc();
  if (!frame) {
    Logger::getInstance().error("Failed to allocate audio frame");
    closeEncoder();
    return false;
  }

  frame->format = codecCtx->sample_fmt;
  frame->sample_rate = codecCtx->sample_rate;
  frame->ch_layout = codecCtx->ch_layout;
  frame->nb_samples = codecCtx->frame_size;

  if (av_frame_get_buffer(frame, 0) < 0) {
    Logger::getInstance().error("Failed to allocate audio frame buffer");
    closeEncoder();
    return false;
  }

  pkt = av_packet_alloc();
  if (!pkt) {
    Logger::getInstance().error("Failed to allocate audio packet");
    closeEncoder();
    return false;
  }

  swrCtx = nullptr;
  audioFifo =
      av_audio_fifo_alloc(codecCtx->sample_fmt, codecCtx->ch_layout.nb_channels,
                          codecCtx->frame_size * 4);
  if (!audioFifo) {
    Logger::getInstance().error("Failed to allocate audio FIFO");
    closeEncoder();
    return false;
  }

  // Calculate and cache audio frame duration
  // frameDuration in codecCtx->time_base units
  if (codecCtx->frame_size > 0 && codecCtx->sample_rate > 0) {
    AVRational sampleRate = AVRational{1, codecCtx->sample_rate};
    frameDuration =
        av_rescale_q(codecCtx->frame_size, sampleRate, codecCtx->time_base);
  } else {
    frameDuration = 0;
  }

  encodedSamples = 0;
  basePtsSamples = AV_NOPTS_VALUE;
  nextPts = 0;
  lastPts = AV_NOPTS_VALUE;
  lastInputSampleRate = 0;
  lastInputChannels = 0;

  Logger::getInstance().success(
      "FFmpeg audio encoder initialized successfully");
  return true;
}

void AudioEncoder::closeEncoder() {
  if (audioFifo) {
    av_audio_fifo_free(audioFifo);
    audioFifo = nullptr;
  }
  if (swrCtx) {
    swr_free(&swrCtx);
    swrCtx = nullptr;
  }
  if (pkt) {
    av_packet_free(&pkt);
    pkt = nullptr;
  }
  if (frame) {
    av_frame_free(&frame);
    frame = nullptr;
  }
  if (codecCtx) {
    avcodec_free_context(&codecCtx);
    codecCtx = nullptr;
  }
  encodedSamples = 0;
  basePtsSamples = AV_NOPTS_VALUE;
  nextPts = 0;
  lastPts = AV_NOPTS_VALUE;
  frameDuration = 0;
  lastInputSampleRate = 0;
  lastInputChannels = 0;
}

bool AudioEncoder::ensureResampler(int inputSampleRate, int inputChannels) {
  if (swrCtx && lastInputSampleRate == inputSampleRate &&
      lastInputChannels == inputChannels) {
    return true;
  }

  if (swrCtx) {
    swr_free(&swrCtx);
    swrCtx = nullptr;
  }

  AVChannelLayout inputLayout;
  if (inputChannels == 1) {
    inputLayout = AV_CHANNEL_LAYOUT_MONO;
  } else if (inputChannels == 2) {
    inputLayout = AV_CHANNEL_LAYOUT_STEREO;
  } else {
    Logger::getInstance().error("Unsupported input channel count: " +
                                std::to_string(inputChannels));
    return false;
  }

  int ret =
      swr_alloc_set_opts2(&swrCtx, &codecCtx->ch_layout, codecCtx->sample_fmt,
                          codecCtx->sample_rate, &inputLayout,
                          AV_SAMPLE_FMT_S16, inputSampleRate, 0, nullptr);
  if (ret < 0 || !swrCtx) {
    Logger::getInstance().error("Failed to allocate resampler context");
    return false;
  }

  if (swr_init(swrCtx) < 0) {
    Logger::getInstance().error("Failed to initialize resampler");
    swr_free(&swrCtx);
    swrCtx = nullptr;
    return false;
  }

  lastInputSampleRate = inputSampleRate;
  lastInputChannels = inputChannels;

  Logger::getInstance().info(
      "Audio resampler configured: " + std::to_string(inputSampleRate) + "Hz/" +
      std::to_string(inputChannels) + "ch -> " +
      std::to_string(codecCtx->sample_rate) + "Hz/" +
      std::to_string(codecCtx->ch_layout.nb_channels) + "ch");
  return true;
}

int AudioEncoder::convertToFifo(const uint8_t *pcmData, size_t pcmLength,
                                int inputSampleRate, int inputChannels) {
  if (!swrCtx || !audioFifo)
    return 0;

  int inputSamples = pcmLength / (inputChannels * sizeof(int16_t));
  const uint8_t *inputData[1] = {pcmData};

  int maxOutSamples = av_rescale_rnd(inputSamples, codecCtx->sample_rate,
                                     inputSampleRate, AV_ROUND_UP);
  if (maxOutSamples <= 0)
    return 0;

  uint8_t **converted = nullptr;
  int ret = av_samples_alloc_array_and_samples(
      &converted, nullptr, codecCtx->ch_layout.nb_channels, maxOutSamples,
      codecCtx->sample_fmt, 0);
  if (ret < 0) {
    Logger::getInstance().error("Failed to allocate resampled buffer");
    return 0;
  }

  int outSamples =
      swr_convert(swrCtx, converted, maxOutSamples, inputData, inputSamples);
  if (outSamples < 0) {
    Logger::getInstance().error("Failed to resample audio");
    av_freep(&converted[0]);
    av_free(converted);
    return 0;
  }

  if (outSamples > 0) {
    int written =
        av_audio_fifo_write(audioFifo, (void **)converted, outSamples);
    if (written < outSamples) {
      Logger::getInstance().warn("Audio FIFO write truncated");
    }
  }

  av_freep(&converted[0]);
  av_free(converted);
  return outSamples;
}

AVPacket *AudioEncoder::encodePCM(const uint8_t *pcmData, size_t pcmLength,
                                  int inputSampleRate, int inputChannels,
                                  int64_t timestampMs) {
  if (!codecCtx || !frame || !pkt)
    return nullptr;

  if (!ensureResampler(inputSampleRate, inputChannels)) {
    return nullptr;
  }

  convertToFifo(pcmData, pcmLength, inputSampleRate, inputChannels);

  if (basePtsSamples == AV_NOPTS_VALUE) {
    basePtsSamples = (timestampMs * codecCtx->sample_rate) / 1000;
    encodedSamples = basePtsSamples;
  }

  if (av_audio_fifo_size(audioFifo) < codecCtx->frame_size) {
    return nullptr;
  }

  if (av_frame_make_writable(frame) < 0) {
    Logger::getInstance().error("Failed to make audio frame writable");
    return nullptr;
  }

  int readSamples =
      av_audio_fifo_read(audioFifo, (void **)frame->data, codecCtx->frame_size);
  if (readSamples < codecCtx->frame_size) {
    Logger::getInstance().error("Failed to read enough samples from FIFO");
    return nullptr;
  }

  frame->nb_samples = codecCtx->frame_size;
  frame->pts = encodedSamples;
  encodedSamples += codecCtx->frame_size;

  int ret = avcodec_send_frame(codecCtx, frame);
  if (ret < 0) {
    Logger::getInstance().error("Error sending audio frame to encoder");
    return nullptr;
  }

  ret = avcodec_receive_packet(codecCtx, pkt);
  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
    return nullptr;
  } else if (ret < 0) {
    Logger::getInstance().error("Error receiving audio packet from encoder");
    return nullptr;
  }

  return pkt;
}

int AudioEncoder::getFrameSize() const {
  if (!codecCtx)
    return 0;
  return codecCtx->frame_size;
}
