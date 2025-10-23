#include "HlsMuxer.h"
#include "util/Logger.h"
#include <cstring>

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
    const AVOutputFormat* fmt = av_guess_format("hls", nullptr, nullptr);
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

    videoStream->id = 0;
    videoStream->time_base = AVRational{1, 1000000}; // microseconds

    // Configure HLS options
    std::string playlistName = currentConfig.hlsPrefix + ".m3u8";
    std::string segmentFilename = currentConfig.hlsPrefix + "-seg-%05d.m4s";
    std::string initFilename = "init.mp4";

    av_opt_set(fmtCtx->priv_data, "hls_segment_type", "fmp4", 0);
    av_opt_set(fmtCtx->priv_data, "hls_fmp4_init_filename", initFilename.c_str(), 0);
    av_opt_set_int(fmtCtx->priv_data, "hls_time", currentConfig.segmentSeconds, 0);
    av_opt_set(fmtCtx->priv_data, "hls_flags", "independent_segments+temp_file", 0);
    av_opt_set(fmtCtx->priv_data, "hls_playlist_type", "vod", 0);
    av_opt_set(fmtCtx->priv_data, "hls_segment_filename", segmentFilename.c_str(), 0);

    // Install custom IO callbacks
    avioSink.installIOCallbacks(fmtCtx);

    // Set the output URL (logical name for the playlist)
    fmtCtx->url = av_strdup(playlistName.c_str());

    Util::Logger::getInstance().success("HLS muxer initialized");
    return true;
}

void HlsMuxer::closeMuxer() {
    if (fmtCtx) {
        avformat_free_context(fmtCtx);
        fmtCtx = nullptr;
    }
    videoStream = nullptr;
    headerWritten = false;
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

    // Rescale timestamps to stream time base if needed
    AVPacket* pktCopy = av_packet_clone(pkt);
    if (!pktCopy) return false;

    pktCopy->stream_index = videoStream->index;
    
    // Rescale timestamps from encoder time base (1/1000000) to stream time base
    av_packet_rescale_ts(pktCopy, AVRational{1, 1000000}, videoStream->time_base);

    int ret = av_interleaved_write_frame(fmtCtx, pktCopy);
    av_packet_free(&pktCopy);

    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        Util::Logger::getInstance().error(std::string("Failed to write packet: ") + errbuf);
        return false;
    }

    lastPts = pkt->pts;
    return true;
}

void HlsMuxer::finalize() {
    if (!fmtCtx || !headerWritten) return;

    int ret = av_write_trailer(fmtCtx);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        Util::Logger::getInstance().warn(std::string("Failed to write trailer: ") + errbuf);
    } else {
        Util::Logger::getInstance().success("HLS trailer written");
    }

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

