#include "HlsMuxer.h"

#include "util/Logger.h"

extern "C" {
#include <libavutil/opt.h>

int rename(const char* oldpath, const char* newpath) { return 0; }
}

namespace {
static constexpr const char* kHlsFormatName = "hls";
static constexpr const char* kHlsSegmentType = "fmp4";
}  // namespace

HlsMuxer::HlsMuxer() = default;

HlsMuxer::~HlsMuxer() { Shutdown(); }

bool HlsMuxer::Initialize(const HlsMuxerConfig& config, HlsFileCallback file_callback) {
  muxerConfig_ = config;
  avioSink_.SetFileCallback(file_callback);
  return OpenMuxer();
}

bool HlsMuxer::OpenMuxer() {
  // Allocate output format context for HLS
  const AVOutputFormat* fmt = av_guess_format(kHlsFormatName, nullptr, nullptr);
  if (!fmt) {
    Logger::GetInstance().Error("HLS muxer not found");
    return false;
  }

  int ret = avformat_alloc_output_context2(&fmtCtx_, fmt, nullptr, nullptr);
  if (ret < 0 || !fmtCtx_) {
    Logger::GetInstance().Error("Failed to allocate HLS output context");
    return false;
  }

  // Create video stream
  videoStream_ = avformat_new_stream(fmtCtx_, nullptr);
  if (!videoStream_) {
    Logger::GetInstance().Error("Failed to create video stream");
    CloseMuxer();
    return false;
  }

  // Do NOT set videoStream_->time_base here - let avformat_write_header choose
  // it
  videoStream_->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
  videoStream_->codecpar->codec_id = AV_CODEC_ID_H264;

  // Create audio stream
  audioStream_ = avformat_new_stream(fmtCtx_, nullptr);
  if (!audioStream_) {
    Logger::GetInstance().Error("Failed to create audio stream");
    CloseMuxer();
    return false;
  }

  // Do NOT set audioStream_->time_base here - let avformat_write_header choose
  // it
  audioStream_->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
  audioStream_->codecpar->codec_id = AV_CODEC_ID_AAC;

  // Configure HLS options
  std::string playlist_name = muxerConfig_.hls_prefix + ".m3u8";
  std::string init_filename = muxerConfig_.hls_prefix + "-init.mp4";
  std::string segment_filename = muxerConfig_.hls_prefix + "-part-%05d.m4s";

  av_opt_set(fmtCtx_->priv_data, "hls_segment_type", kHlsSegmentType, 0);
  av_opt_set(fmtCtx_->priv_data, "hls_fmp4_init_filename", init_filename.c_str(), 0);
  av_opt_set_int(fmtCtx_->priv_data, "hls_time", muxerConfig_.hls_time_seconds, 0);
  // Write playlist directly (no temp_file rename), so our in-memory IO sink receives an
  // update on each write. Keep segments independent; append_list grows EVENT playlist.
  av_opt_set(fmtCtx_->priv_data, "hls_flags", "independent_segments+append_list", 0);
  // Force the configured playlist type (default: event)
  av_opt_set(fmtCtx_->priv_data, "hls_playlist_type", muxerConfig_.playlist_type.c_str(), 0);
  av_opt_set_int(fmtCtx_->priv_data, "hls_list_size", muxerConfig_.hls_list_size, 0);
  av_opt_set(fmtCtx_->priv_data, "hls_segment_filename", segment_filename.c_str(), 0);
  av_opt_set(fmtCtx_->priv_data, "method", "PUT", 0);

  // Install custom IO callbacks
  avioSink_.InstallIoCallbacks(fmtCtx_);

  // Set the output URL (logical name for the playlist)
  fmtCtx_->url = av_strdup(playlist_name.c_str());

  Logger::GetInstance().Success("HLS muxer initialized");
  return true;
}

void HlsMuxer::CloseMuxer() {
  if (fmtCtx_) {
    // Free the URL string we allocated
    av_free(fmtCtx_->url);
    fmtCtx_->url = nullptr;
    avformat_free_context(fmtCtx_);
    fmtCtx_ = nullptr;
  }
  videoStream_ = nullptr;
  audioStream_ = nullptr;
  headerWritten_ = false;
  videoCodecTimeBase_ = {0, 0};
  audioCodecTimeBase_ = {0, 0};
}

bool HlsMuxer::Start() {
  if (!fmtCtx_ || !videoStream_) {
    Logger::GetInstance().Error("Muxer not initialized");
    return false;
  }

  if (headerWritten_) {
    Logger::GetInstance().Warn("Header already written");
    return true;
  }

  // Capture encoder time base from video stream if available; fallback to
  // microseconds
  if (videoStream_->time_base.num != 0 && videoStream_->time_base.den != 0) {
    videoCodecTimeBase_ = videoStream_->time_base;
  } else {
    videoCodecTimeBase_ = AVRational{1, 1000000};
  }

  // Capture encoder time base from audio stream if available; fallback to
  // microseconds
  if (audioStream_ && audioStream_->time_base.num != 0 && audioStream_->time_base.den != 0) {
    audioCodecTimeBase_ = audioStream_->time_base;
  } else {
    audioCodecTimeBase_ = AVRational{1, 1000000};
  }

  int ret = avformat_write_header(fmtCtx_, nullptr);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    Logger::GetInstance().Error(std::string("Failed to write HLS header: ") + errbuf);
    return false;
  }

  headerWritten_ = true;
  Logger::GetInstance().Success("HLS header written");
  return true;
}

bool HlsMuxer::ConfigureAudioStream(const AVCodecContext* audio_ctx) {
  if (!audioStream_ || !audio_ctx) {
    Logger::GetInstance().Error("configureAudioStream: null stream or context");
    return false;
  }

  AVCodecParameters* codecpar = audioStream_->codecpar;
  int ret = avcodec_parameters_from_context(codecpar, audio_ctx);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    Logger::GetInstance().Error(std::string("avcodec_parameters_from_context failed: ") + errbuf);
    return false;
  }

  codecpar->format = audio_ctx->sample_fmt;
  ret = av_channel_layout_copy(&codecpar->ch_layout, &audio_ctx->ch_layout);
  if (ret < 0) {
    Logger::GetInstance().Error("Failed to copy audio channel layout");
    return false;
  }

  audioStream_->time_base = audio_ctx->time_base;

  Logger::GetInstance().Info(std::string("Audio stream configured: ") +
                             std::to_string(codecpar->sample_rate) + "Hz, " +
                             std::to_string(codecpar->ch_layout.nb_channels) + " channels");
  return true;
}

bool HlsMuxer::WriteVideoPacket(AVPacket* pkt) {
  if (!fmtCtx_ || !videoStream_ || !headerWritten_ || !pkt) {
    return false;
  }

  // Make a stack copy by referencing the original packet
  AVPacket pkt_local = {};
  if (av_packet_ref(&pkt_local, pkt) < 0) {
    return false;
  }

  pkt_local.stream_index = videoStream_->index;

  // Rescale timestamps from encoder time base to stream time base
  av_packet_rescale_ts(&pkt_local, videoCodecTimeBase_, videoStream_->time_base);

  int ret = av_interleaved_write_frame(fmtCtx_, &pkt_local);
  av_packet_unref(&pkt_local);

  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    Logger::GetInstance().Error(std::string("Failed to write video packet: ") + errbuf);
    return false;
  }
  return true;
}

bool HlsMuxer::WriteAudioPacket(AVPacket* pkt) {
  if (!fmtCtx_ || !audioStream_ || !headerWritten_ || !pkt) {
    return false;
  }

  // Make a stack copy by referencing the original packet (no heap alloc)
  AVPacket pkt_local = {};
  if (av_packet_ref(&pkt_local, pkt) < 0) {
    return false;
  }

  pkt_local.stream_index = audioStream_->index;

  // Rescale timestamps from encoder time base to stream time base
  av_packet_rescale_ts(&pkt_local, audioCodecTimeBase_, audioStream_->time_base);

  int ret = av_interleaved_write_frame(fmtCtx_, &pkt_local);
  av_packet_unref(&pkt_local);

  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    Logger::GetInstance().Error(std::string("Failed to write audio packet: ") + errbuf);
    return false;
  }
  return true;
}

void HlsMuxer::Finalize() {
  if (!fmtCtx_ || !headerWritten_) {
    return;
  }

  Logger::GetInstance().Info("Writing trailer");

  int ret = av_write_trailer(fmtCtx_);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    Logger::GetInstance().Warn(std::string("Failed to write trailer: ") + errbuf);
  } else {
    Logger::GetInstance().Success("HLS trailer written");
  }

  // Ensure ENDLIST is present for seekable playback with EVENT playlists.
  avioSink_.EmitEndlistIfMissing();

  Logger::GetInstance().Info("HLS muxer finalized");

  headerWritten_ = false;
}

void HlsMuxer::Shutdown() {
  Finalize();
  CloseMuxer();
}

AVCodecParameters* HlsMuxer::GetVideoCodecParams() {
  if (!videoStream_) {
    return nullptr;
  }
  return videoStream_->codecpar;
}

AVCodecParameters* HlsMuxer::GetAudioCodecParams() {
  if (!audioStream_) {
    return nullptr;
  }
  return audioStream_->codecpar;
}
