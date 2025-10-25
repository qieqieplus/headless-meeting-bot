#include "FFmpegEncoder.h"
#include "util/Logger.h"
#include <cstring>

FFmpegEncoder::FFmpegEncoder() = default;

FFmpegEncoder::~FFmpegEncoder() {
    shutdown();
}

bool FFmpegEncoder::initialize(const FFmpegEncoderConfig& config) {
    currentConfig = config;
    return openEncoder();
}

bool FFmpegEncoder::reinitialize(const FFmpegEncoderConfig& config) {
    shutdown();
    currentConfig = config;
    return openEncoder();
}

void FFmpegEncoder::shutdown() {
    closeEncoder();
}

const AVCodec* FFmpegEncoder::selectCodec(const std::string& encoder) {
    const AVCodec* codec = nullptr;

    if (encoder == "nvenc") {
        codec = avcodec_find_encoder_by_name("h264_nvenc");
    }

    if (!codec) {
        codec = avcodec_find_encoder_by_name("libx264");
    }

    if (codec) {
        Util::Logger::getInstance().info(std::string("Encoder: ") + codec->name);
    } else {
        Util::Logger::getInstance().error("No H.264 encoder available");
    }

    return codec;
}

bool FFmpegEncoder::configureEncoder(AVCodecContext* ctx, const AVCodec* codec) {
    ctx->width = currentConfig.width;
    ctx->height = currentConfig.height;
    ctx->time_base = AVRational{1, 1000000}; // microseconds
    ctx->framerate = AVRational{currentConfig.fps, 1};
    ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    ctx->bit_rate = currentConfig.bitrateKbps * 1000;
    ctx->gop_size = currentConfig.fps * currentConfig.gopSeconds;
    ctx->max_b_frames = 0; // No B-frames for low latency
    
    bool isNvenc = (strstr(codec->name, "nvenc") != nullptr);
    
    if (isNvenc) {
        // NVENC-specific options
        av_opt_set(ctx->priv_data, "preset", "fast", 0);
        av_opt_set(ctx->priv_data, "rc", "cbr_ld_hq", 0);
        av_opt_set(ctx->priv_data, "zerolatency", "1", 0);
        av_opt_set(ctx->priv_data, "profile", currentConfig.profile.c_str(), 0);
    } else {
        // libx264 options
        av_opt_set(ctx->priv_data, "preset", currentConfig.preset.c_str(), 0);
        av_opt_set(ctx->priv_data, "tune", "zerolatency", 0);
        av_opt_set(ctx->priv_data, "profile", currentConfig.profile.c_str(), 0);
    }
    
    // Global header for MP4/HLS
    ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    
    return true;
}

bool FFmpegEncoder::openEncoder() {
    if (currentConfig.width <= 0 || currentConfig.height <= 0 || currentConfig.fps <= 0) {
        Util::Logger::getInstance().error("Invalid encoder config");
        return false;
    }
    
    const AVCodec* codec = selectCodec(currentConfig.encoder);
    if (!codec) return false;
    
    codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        Util::Logger::getInstance().error("Failed to allocate codec context");
        return false;
    }
    
    if (!configureEncoder(codecCtx, codec)) {
        closeEncoder();
        return false;
    }
    
    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        Util::Logger::getInstance().error("Failed to open codec");
        closeEncoder();
        return false;
    }
    
    frame = av_frame_alloc();
    if (!frame) {
        Util::Logger::getInstance().error("Failed to allocate frame");
        closeEncoder();
        return false;
    }
    
    frame->format = codecCtx->pix_fmt;
    frame->width = codecCtx->width;
    frame->height = codecCtx->height;
    
    if (av_frame_get_buffer(frame, 0) < 0) {
        Util::Logger::getInstance().error("Failed to allocate frame buffer");
        closeEncoder();
        return false;
    }
    
    pkt = av_packet_alloc();
    if (!pkt) {
        Util::Logger::getInstance().error("Failed to allocate packet");
        closeEncoder();
        return false;
    }
    
    frameCount = 0;
    Util::Logger::getInstance().success("FFmpeg encoder initialized successfully");
    return true;
}

void FFmpegEncoder::closeEncoder() {
    /*
    if (swsCtx) {
        sws_freeContext(swsCtx);
        swsCtx = nullptr;
    }
    */
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
    frameCount = 0;
}

AVFrame* FFmpegEncoder::convertToAVFrame(const uint8_t* yPlane, const uint8_t* uPlane,
                                         const uint8_t* vPlane, int width, int height,
                                         int64_t ptsUs) {
    if (!frame || !codecCtx) return nullptr;
    
    if (av_frame_make_writable(frame) < 0) {
        Util::Logger::getInstance().error("Failed to make frame writable");
        return nullptr;
    }
    
    // Copy I420 planes to AVFrame
    int yStride = frame->linesize[0];
    int uStride = frame->linesize[1];
    int vStride = frame->linesize[2];
    
    for (int y = 0; y < height; ++y) {
        std::memcpy(frame->data[0] + y * yStride, yPlane + y * width, width);
    }
    
    int chromaHeight = height / 2;
    int chromaWidth = width / 2;
    for (int y = 0; y < chromaHeight; ++y) {
        std::memcpy(frame->data[1] + y * uStride, uPlane + y * chromaWidth, chromaWidth);
        std::memcpy(frame->data[2] + y * vStride, vPlane + y * chromaWidth, chromaWidth);
    }
    
    frame->pts = ptsUs;
    
    if (forceKeyframe.exchange(false)) {
        frame->pict_type = AV_PICTURE_TYPE_I;
        // frame->flags |= AV_FRAME_FLAG_KEY;
    } else {
        frame->pict_type = AV_PICTURE_TYPE_NONE;
    }
    
    return frame;
}

AVPacket* FFmpegEncoder::encodeI420(const uint8_t* yPlane, const uint8_t* uPlane,
                                    const uint8_t* vPlane, int width, int height,
                                    int64_t ptsUs) {
    if (!codecCtx || !frame || !pkt) return nullptr;
    
    if (width != currentConfig.width || height != currentConfig.height) {
        Util::Logger::getInstance().error("Frame dimensions don't match encoder config");
        return nullptr;
    }
    
    AVFrame* inputFrame = convertToAVFrame(yPlane, uPlane, vPlane, width, height, ptsUs);
    if (!inputFrame) return nullptr;
    
    int ret = avcodec_send_frame(codecCtx, inputFrame);
    if (ret < 0) {
        Util::Logger::getInstance().error("Error sending frame to encoder");
        return nullptr;
    }
    
    ret = avcodec_receive_packet(codecCtx, pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return nullptr; // Need more frames or encoder flushed
    } else if (ret < 0) {
        Util::Logger::getInstance().error("Error receiving packet from encoder");
        return nullptr;
    }
    
    frameCount++;
    return pkt; // Caller must call av_packet_unref(pkt)
}

void FFmpegEncoder::requestIDR() {
    forceKeyframe.store(true);
}

