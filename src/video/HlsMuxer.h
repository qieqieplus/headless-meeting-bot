#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include "AvioMemorySink.h"
#include "FFmpegEncoder.h"

extern "C" {
#include <libavformat/avformat.h>
}

struct HlsMuxerConfig {
    int width = 1280;
    int height = 720;
    int fps = 30;
    int segmentSeconds = 2;
    std::string hlsPrefix = "media"; // Base name for playlist and segments
};

// HLS muxer with fMP4 segments
class HlsMuxer {
public:
    HlsMuxer();
    ~HlsMuxer();

    // Initialize muxer with config and file callback
    bool initialize(const HlsMuxerConfig& config, HlsFileCallback fileCallback);
    
    // Start muxing (writes header and init segment)
    bool start();
    
    // Write an encoded packet to the muxer
    bool writePacket(AVPacket* pkt);
    
    // Finalize muxing (writes trailer and final playlist with EXT-X-ENDLIST)
    void finalize();
    
    // Shutdown muxer
    void shutdown();

    AVCodecParameters* getVideoCodecParams();

private:
    bool openMuxer();
    void closeMuxer();

private:
    HlsMuxerConfig currentConfig;
    AVFormatContext* fmtCtx = nullptr;
    AVStream* videoStream = nullptr;
    AvioMemorySink avioSink;
    bool headerWritten = false;
    int64_t lastPts = AV_NOPTS_VALUE;
};

