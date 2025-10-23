#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

struct FFmpegEncoderConfig {
    int width = 0;
    int height = 0;
    int fps = 30;
    int bitrateKbps = 2500;
    int gopSeconds = 2;              // Keyframe interval in seconds
    std::string encoder = "auto";    // "auto", "x264", "nvenc"
    std::string preset = "veryfast"; // x264: ultrafast..veryslow; nvenc: fast,medium,slow
    std::string profile = "main";    // "baseline", "main", "high"
};

// FFmpeg-based H.264 encoder with libx264 and h264_nvenc support
class FFmpegEncoder {
public:
    FFmpegEncoder();
    ~FFmpegEncoder();

    // Initialize encoder with given config
    bool initialize(const FFmpegEncoderConfig& config);
    
    // Reinitialize with new config (closes and reopens encoder)
    bool reinitialize(const FFmpegEncoderConfig& config);
    
    // Shutdown encoder
    void shutdown();

    // Encode one I420 frame and return encoded packet
    // Returns AVPacket* on success (caller must call av_packet_unref), nullptr on failure
    // Input planes must be contiguous with standard strides: Y=width, U=V=width/2
    AVPacket* encodeI420(const uint8_t* yPlane,
                         const uint8_t* uPlane,
                         const uint8_t* vPlane,
                         int width,
                         int height,
                         int64_t ptsUs);

    // Request next frame to be a keyframe (IDR)
    void requestIDR();

    int getWidth() const { return currentConfig.width; }
    int getHeight() const { return currentConfig.height; }
    int getFps() const { return currentConfig.fps; }
    AVCodecContext* getCodecContext() { return codecCtx; }

private:
    bool openEncoder();
    void closeEncoder();
    const AVCodec* selectCodec(const std::string& encoderName);
    bool configureEncoder(AVCodecContext* ctx, const AVCodec* codec);
    AVFrame* convertToAVFrame(const uint8_t* yPlane, const uint8_t* uPlane, const uint8_t* vPlane,
                              int width, int height, int64_t ptsUs);

private:
    FFmpegEncoderConfig currentConfig;
    AVCodecContext* codecCtx = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* swsCtx = nullptr;
    int64_t frameCount = 0;
    bool forceKeyframe = false;
};

