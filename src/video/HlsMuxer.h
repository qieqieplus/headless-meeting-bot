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
    int width = 0;        // auto-detect
    int height = 0;       // auto-detect
    int fps = 30;
    int segmentSeconds = 10;
    std::string hlsPrefix = "media"; // Base name for playlist and segments
    std::string playlistType = "vod"; // "vod", "live", or "event"
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
    AVRational videoCodecTimeBase = {0, 0}; // Time base from encoder
};

