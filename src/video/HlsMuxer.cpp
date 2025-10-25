#include "HlsMuxer.h"
#include "util/Logger.h"
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace {
    static constexpr const char* HLS_FORMAT_NAME = "hls";
    static constexpr const char* HLS_SEGMENT_TYPE = "fmp4";
}

HlsMuxer::HlsMuxer() = default;

HlsMuxer::~HlsMuxer() {
    shutdown();
}

bool HlsMuxer::initialize(const HlsMuxerConfig& config, HlsFileCallback fileCallback) {
    currentConfig = config;
    avioSink.setFileCallback(fileCallback);
    return openMuxer();
}

bool HlsMuxer::openMuxer() {
    // Allocate output format context for HLS
    const AVOutputFormat* fmt = av_guess_format(HLS_FORMAT_NAME, nullptr, nullptr);
    if (!fmt) {
        Util::Logger::getInstance().error("HLS muxer not found");
        return false;
    }

    int ret = avformat_alloc_output_context2(&fmtCtx, fmt, nullptr, nullptr);
    if (ret < 0 || !fmtCtx) {
        Util::Logger::getInstance().error("Failed to allocate HLS output context");
        return false;
    }

    // Create video stream
    videoStream = avformat_new_stream(fmtCtx, nullptr);
    if (!videoStream) {
        Util::Logger::getInstance().error("Failed to create video stream");
        closeMuxer();
        return false;
    }

    // Do NOT set videoStream->time_base here - let avformat_write_header choose it
    videoStream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    videoStream->codecpar->codec_id = AV_CODEC_ID_H264;

    // Configure HLS options
    std::string playlistName = currentConfig.hlsPrefix + ".m3u8";
    std::string initFilename = currentConfig.hlsPrefix + "-init.mp4";
    std::string segmentFilename = currentConfig.hlsPrefix + "-part-%05d.m4s";

    av_opt_set(fmtCtx->priv_data, "hls_segment_type", HLS_SEGMENT_TYPE, 0);
    av_opt_set(fmtCtx->priv_data, "hls_fmp4_init_filename", initFilename.c_str(), 0);
    av_opt_set_int(fmtCtx->priv_data, "hls_time", currentConfig.segmentSeconds, 0);
    // Avoid temp_file since we're not writing to disk
    av_opt_set(fmtCtx->priv_data, "hls_flags", "independent_segments", 0);
    av_opt_set(fmtCtx->priv_data, "hls_playlist_type", currentConfig.playlistType.c_str(), 0);
    av_opt_set(fmtCtx->priv_data, "hls_segment_filename", segmentFilename.c_str(), 0);
    av_opt_set(fmtCtx->priv_data, "method", "PUT", 0);

    // Install custom IO callbacks
    avioSink.installIOCallbacks(fmtCtx);

    // Set the output URL (logical name for the playlist)
    fmtCtx->url = av_strdup(playlistName.c_str());

    Util::Logger::getInstance().success("HLS muxer initialized");
    return true;
}

void HlsMuxer::closeMuxer() {
    if (fmtCtx) {
        // Free the URL string we allocated
        av_free(fmtCtx->url);
        fmtCtx->url = nullptr;
        avformat_free_context(fmtCtx);
        fmtCtx = nullptr;
    }
    videoStream = nullptr;
    headerWritten = false;
    videoCodecTimeBase = {0, 0};
}

bool HlsMuxer::start() {
    if (!fmtCtx || !videoStream) {
        Util::Logger::getInstance().error("Muxer not initialized");
        return false;
    }

    if (headerWritten) {
        Util::Logger::getInstance().warn("Header already written");
        return true;
    }

    // Capture encoder time base from stream if available; fallback to microseconds
    if (videoStream->time_base.num != 0 &&
        videoStream->time_base.den != 0) {
        videoCodecTimeBase = videoStream->time_base;
    } else {
        videoCodecTimeBase = AVRational{1, 1000000};
    }

    int ret = avformat_write_header(fmtCtx, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        Util::Logger::getInstance().error(std::string("Failed to write HLS header: ") + errbuf);
        return false;
    }

    headerWritten = true;
    Util::Logger::getInstance().success("HLS header written");
    return true;
}

bool HlsMuxer::writePacket(AVPacket* pkt) {
    if (!fmtCtx || !videoStream || !headerWritten || !pkt) {
        return false;
    }

    // Make a stack copy by referencing the original packet (no heap alloc)
    AVPacket pktLocal = {};  // Initialize to zero (replaces deprecated av_init_packet)
    if (av_packet_ref(&pktLocal, pkt) < 0) {
        return false;
    }

    pktLocal.stream_index = videoStream->index;

    // Rescale timestamps from encoder time base to stream time base
    av_packet_rescale_ts(&pktLocal, videoCodecTimeBase, videoStream->time_base);

    int ret = av_interleaved_write_frame(fmtCtx, &pktLocal);
    av_packet_unref(&pktLocal);

    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        Util::Logger::getInstance().error(std::string("Failed to write packet: ") + errbuf);
        return false;
    }
    return true;
}

void HlsMuxer::finalize() {
    if (!fmtCtx || !headerWritten) return;

    Util::Logger::getInstance().info("Writing trailer");

    int ret = av_write_trailer(fmtCtx);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        Util::Logger::getInstance().warn(std::string("Failed to write trailer: ") + errbuf);
    } else {
        Util::Logger::getInstance().success("HLS trailer written");
    }

    Util::Logger::getInstance().info("HLS muxer finalized");

    headerWritten = false;
}

void HlsMuxer::shutdown() {
    finalize();
    closeMuxer();
}

AVCodecParameters* HlsMuxer::getVideoCodecParams() {
    if (!videoStream) return nullptr;
    return videoStream->codecpar;
}
