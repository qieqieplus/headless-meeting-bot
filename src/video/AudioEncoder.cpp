#include "AudioEncoder.h"

#include "util/Logger.h"

AudioEncoder::AudioEncoder() = default;

AudioEncoder::~AudioEncoder() { Shutdown(); }

bool AudioEncoder::Initialize(const AudioEncoderConfig& config) {
  audioConfig_ = config;
  return OpenEncoder();
}

bool AudioEncoder::Reinitialize(const AudioEncoderConfig& config) {
  Shutdown();
  audioConfig_ = config;
  return OpenEncoder();
}

void AudioEncoder::Shutdown() { CloseEncoder(); }

const AVCodec* AudioEncoder::SelectCodec(const std::string& codec_name) {
  const AVCodec* codec = nullptr;

  if (codec_name == "libfdk_aac") {
    codec = avcodec_find_encoder_by_name("libfdk_aac");
  }

  if (!codec) {
    codec = avcodec_find_encoder_by_name("aac");
  }

  if (codec) {
    Logger::GetInstance().Info(std::string("Audio Encoder: ") + codec->name);
  } else {
    Logger::GetInstance().Error("No AAC encoder available");
  }

  return codec;
}

bool AudioEncoder::ConfigureEncoder(AVCodecContext* ctx, const AVCodec* codec) {
  ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;  // AAC typically uses planar float
  ctx->sample_rate = audioConfig_.sample_rate;
  ctx->bit_rate = audioConfig_.bitrate_kbps * 1000;
  ctx->time_base = AVRational{1, audioConfig_.sample_rate};

  // Set channel layout
  if (audioConfig_.channels == 1) {
    ctx->ch_layout = AV_CHANNEL_LAYOUT_MONO;
  } else if (audioConfig_.channels == 2) {
    ctx->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
  } else {
    Logger::GetInstance().Error("Unsupported channel count: " +
                                std::to_string(audioConfig_.channels));
    return false;
  }

  ctx->profile = FF_PROFILE_AAC_LOW;
  ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  return true;
}

bool AudioEncoder::OpenEncoder() {
  if (audioConfig_.sample_rate <= 0 || audioConfig_.channels <= 0) {
    Logger::GetInstance().Error("Invalid audio encoder config");
    return false;
  }

  const AVCodec* codec = SelectCodec(audioConfig_.codec);
  if (!codec) {
    return false;
  }

  codecCtx_ = avcodec_alloc_context3(codec);
  if (!codecCtx_) {
    Logger::GetInstance().Error("Failed to allocate audio codec context");
    return false;
  }

  if (!ConfigureEncoder(codecCtx_, codec)) {
    CloseEncoder();
    return false;
  }

  if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
    Logger::GetInstance().Error("Failed to open audio codec");
    CloseEncoder();
    return false;
  }

  Logger::GetInstance().Info("Audio codec opened: " + std::to_string(codecCtx_->sample_rate) +
                             "Hz, " + std::to_string(codecCtx_->ch_layout.nb_channels) +
                             " channels, " + "frame_size=" + std::to_string(codecCtx_->frame_size));

  frame_ = av_frame_alloc();
  if (!frame_) {
    Logger::GetInstance().Error("Failed to allocate audio frame");
    CloseEncoder();
    return false;
  }

  frame_->format = codecCtx_->sample_fmt;
  frame_->sample_rate = codecCtx_->sample_rate;
  frame_->ch_layout = codecCtx_->ch_layout;
  frame_->nb_samples = codecCtx_->frame_size;

  if (av_frame_get_buffer(frame_, 0) < 0) {
    Logger::GetInstance().Error("Failed to allocate audio frame buffer");
    CloseEncoder();
    return false;
  }

  swrCtx_ = nullptr;
  audioFifo_ = av_audio_fifo_alloc(codecCtx_->sample_fmt, codecCtx_->ch_layout.nb_channels,
                                   codecCtx_->frame_size * 4);
  if (!audioFifo_) {
    Logger::GetInstance().Error("Failed to allocate audio FIFO");
    CloseEncoder();
    return false;
  }

  // Calculate and cache audio frame duration
  // frameDuration in codecCtx_->time_base units
  if (codecCtx_->frame_size > 0 && codecCtx_->sample_rate > 0) {
    auto sample_rate = AVRational{1, codecCtx_->sample_rate};
    frameDuration_ = av_rescale_q(codecCtx_->frame_size, sample_rate, codecCtx_->time_base);
  } else {
    frameDuration_ = 0;
  }

  nextPts_ = AV_NOPTS_VALUE;
  lastInputSampleRate_ = 0;
  lastInputChannels_ = 0;

  Logger::GetInstance().Success("FFmpeg audio encoder initialized successfully");
  return true;
}

void AudioEncoder::CloseEncoder() {
  // Clear packet queue
  while (!packetQueue_.empty()) {
    AVPacket* pkt = packetQueue_.front();
    packetQueue_.pop();
    av_packet_free(&pkt);
  }

  if (audioFifo_) {
    av_audio_fifo_free(audioFifo_);
    audioFifo_ = nullptr;
  }
  if (swrCtx_) {
    swr_free(&swrCtx_);
    swrCtx_ = nullptr;
  }
  if (frame_) {
    av_frame_free(&frame_);
    frame_ = nullptr;
  }
  if (codecCtx_) {
    avcodec_free_context(&codecCtx_);
    codecCtx_ = nullptr;
  }
  nextPts_ = AV_NOPTS_VALUE;
  frameDuration_ = 0;
  lastInputSampleRate_ = 0;
  lastInputChannels_ = 0;
}

bool AudioEncoder::EnsureResampler(int input_sample_rate, int input_channels) {
  if (swrCtx_ && lastInputSampleRate_ == input_sample_rate &&
      lastInputChannels_ == input_channels) {
    return true;
  }

  if (swrCtx_) {
    swr_free(&swrCtx_);
    swrCtx_ = nullptr;
  }

  AVChannelLayout input_layout;
  if (input_channels == 1) {
    input_layout = AV_CHANNEL_LAYOUT_MONO;
  } else if (input_channels == 2) {
    input_layout = AV_CHANNEL_LAYOUT_STEREO;
  } else {
    Logger::GetInstance().Error("Unsupported input channel count: " +
                                std::to_string(input_channels));
    return false;
  }

  int ret = swr_alloc_set_opts2(&swrCtx_, &codecCtx_->ch_layout, codecCtx_->sample_fmt,
                                codecCtx_->sample_rate, &input_layout, AV_SAMPLE_FMT_S16,
                                input_sample_rate, 0, nullptr);
  if (ret < 0 || !swrCtx_) {
    Logger::GetInstance().Error("Failed to allocate resampler context");
    return false;
  }

  if (swr_init(swrCtx_) < 0) {
    Logger::GetInstance().Error("Failed to initialize resampler");
    swr_free(&swrCtx_);
    swrCtx_ = nullptr;
    return false;
  }

  lastInputSampleRate_ = input_sample_rate;
  lastInputChannels_ = input_channels;

  Logger::GetInstance().Info("Audio resampler configured: " + std::to_string(input_sample_rate) +
                             "Hz/" + std::to_string(input_channels) + "ch -> " +
                             std::to_string(codecCtx_->sample_rate) + "Hz/" +
                             std::to_string(codecCtx_->ch_layout.nb_channels) + "ch");
  return true;
}

int AudioEncoder::ConvertToFifo(const uint8_t* pcm_data, size_t pcm_length, int input_sample_rate,
                                int input_channels) {
  if (!swrCtx_ || !audioFifo_) {
    return 0;
  }

  int input_samples = pcm_length / (input_channels * sizeof(int16_t));
  const uint8_t* input_data[1] = {pcm_data};

  int max_out_samples =
      av_rescale_rnd(input_samples, codecCtx_->sample_rate, input_sample_rate, AV_ROUND_UP);
  if (max_out_samples <= 0) {
    return 0;
  }

  uint8_t** converted = nullptr;
  int ret =
      av_samples_alloc_array_and_samples(&converted, nullptr, codecCtx_->ch_layout.nb_channels,
                                         max_out_samples, codecCtx_->sample_fmt, 0);
  if (ret < 0) {
    Logger::GetInstance().Error("Failed to allocate resampled buffer");
    return 0;
  }

  int out_samples = swr_convert(swrCtx_, converted, max_out_samples, input_data, input_samples);
  if (out_samples < 0) {
    Logger::GetInstance().Error("Failed to resample audio");
    av_freep(&converted[0]);
    av_free(converted);
    return 0;
  }

  if (out_samples > 0) {
    int written = av_audio_fifo_write(audioFifo_, reinterpret_cast<void**>(converted), out_samples);
    if (written < out_samples) {
      Logger::GetInstance().Warn("Audio FIFO write truncated");
    }
  }

  av_freep(&converted[0]);
  av_free(converted);
  return out_samples;
}

bool AudioEncoder::EncodePcm(const uint8_t* pcm_data, size_t pcm_length, int input_sample_rate,
                             int input_channels, int64_t timestamp_ms) {
  if (!codecCtx_ || !frame_) {
    return false;
  }

  if (!EnsureResampler(input_sample_rate, input_channels)) {
    return false;
  }

  // Convert incoming timestamp to current PTS in sample units (encoder's native
  // timebase)
  int64_t current_pts = (timestamp_ms * codecCtx_->sample_rate) / 1000;

  if (nextPts_ == AV_NOPTS_VALUE) {
    nextPts_ = current_pts;
  } else {
    // Detect timestamp gap (e.g., network pause, speaker change)
    int64_t gap_samples = current_pts - nextPts_;
    int64_t max_gap_samples = codecCtx_->frame_size * 2;
    if (gap_samples > max_gap_samples) {
      Logger::GetInstance().Warn("Resetting nextPts to currentPts to maintain A/V sync.");
      nextPts_ = current_pts;
    }
  }

  int converted = ConvertToFifo(pcm_data, pcm_length, input_sample_rate, input_channels);
  if (converted < 0) {
    Logger::GetInstance().Error("Failed to convert audio to FIFO");
    return false;
  }

  // Encode all frames that are ready in the FIFO
  while (av_audio_fifo_size(audioFifo_) >= codecCtx_->frame_size) {
    if (av_frame_make_writable(frame_) < 0) {
      Logger::GetInstance().Error("Failed to make audio frame writable");
      return false;
    }

    int read_samples = av_audio_fifo_read(audioFifo_, reinterpret_cast<void**>(frame_->data),
                                          codecCtx_->frame_size);
    if (read_samples < codecCtx_->frame_size) {
      Logger::GetInstance().Error("Failed to read enough samples from FIFO");
      return false;
    }

    frame_->nb_samples = codecCtx_->frame_size;
    frame_->pts = nextPts_;
    nextPts_ += codecCtx_->frame_size;

    int ret = avcodec_send_frame(codecCtx_, frame_);
    if (ret < 0) {
      Logger::GetInstance().Error("Error sending audio frame to encoder");
      return false;
    }

    // Drain all available packets from encoder
    DrainPackets();
  }

  return true;
}

void AudioEncoder::DrainPackets() {
  if (!codecCtx_) {
    return;
  }

  while (true) {
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
      Logger::GetInstance().Error("Failed to allocate audio packet");
      break;
    }

    int ret = avcodec_receive_packet(codecCtx_, pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      // No more packets available right now (EAGAIN) or encoder flushed (EOF)
      av_packet_free(&pkt);
      break;
    } else if (ret < 0) {
      Logger::GetInstance().Error("Error receiving audio packet from encoder");
      av_packet_free(&pkt);
      break;
    }

    // Successfully received a packet - add to queue
    packetQueue_.push(pkt);
  }
}

AVPacket* AudioEncoder::GetNextPacket() {
  if (packetQueue_.empty()) {
    return nullptr;
  }

  AVPacket* pkt = packetQueue_.front();
  packetQueue_.pop();
  return pkt;  // Caller must call av_packet_free()
}

void AudioEncoder::Flush() {
  if (!codecCtx_) {
    return;
  }

  // Encode any remaining samples in the FIFO (partial frame if needed)
  // For audio, we typically pad with silence if we have partial frames
  while (av_audio_fifo_size(audioFifo_) > 0) {
    if (av_frame_make_writable(frame_) < 0) {
      Logger::GetInstance().Error("Failed to make audio frame writable during flush");
      break;
    }

    int available = av_audio_fifo_size(audioFifo_);
    int to_read = (available < codecCtx_->frame_size) ? available : codecCtx_->frame_size;

    // Read what we have
    int read_samples =
        av_audio_fifo_read(audioFifo_, reinterpret_cast<void**>(frame_->data), to_read);
    if (read_samples <= 0) {
      Logger::GetInstance().Error("Failed to read samples from FIFO during flush");
      break;
    }

    // If partial frame, pad with silence
    if (read_samples < codecCtx_->frame_size) {
      int channels = codecCtx_->ch_layout.nb_channels;
      int bytes_per_sample = av_get_bytes_per_sample(codecCtx_->sample_fmt);

      // Zero out the remaining samples (silence padding)
      for (int ch = 0; ch < channels; ch++) {
        uint8_t* ch_data = frame_->data[ch];
        memset(ch_data + read_samples * bytes_per_sample, 0,
               (codecCtx_->frame_size - read_samples) * bytes_per_sample);
      }
      Logger::GetInstance().Info("Padded partial audio frame: " + std::to_string(read_samples) +
                                 "/" + std::to_string(codecCtx_->frame_size) + " samples");
    }

    frame_->nb_samples = codecCtx_->frame_size;
    frame_->pts = nextPts_;
    nextPts_ += codecCtx_->frame_size;  // Advance by full frame size for consistent timing

    int ret = avcodec_send_frame(codecCtx_, frame_);
    if (ret < 0) {
      Logger::GetInstance().Error("Error sending audio frame during flush");
      break;
    }

    DrainPackets();
  }

  // Send NULL frame to signal end of stream
  int ret = avcodec_send_frame(codecCtx_, nullptr);
  if (ret < 0 && ret != AVERROR_EOF) {
    Logger::GetInstance().Error("Error flushing audio encoder");
    return;
  }

  // Drain all remaining packets
  DrainPackets();
}

int AudioEncoder::GetFrameSize() const {
  if (!codecCtx_) {
    return 0;
  }
  return codecCtx_->frame_size;
}
