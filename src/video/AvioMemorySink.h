#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

extern "C" {
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
}

// Callback invoked when a file is closed by the muxer
// filename: logical name (e.g., "media.m3u8", "init.mp4", "seg-00001.m4s")
// data: full file contents
// size: byte count
// is_playlist: 1 if filename ends with .m3u8, 0 otherwise
// sequence: media sequence number (0 for init/playlist)
using HlsFileCallback = std::function<void(const char* filename,
                                           const uint8_t* data,
                                           size_t size,
                                           int is_playlist,
                                           uint64_t sequence)>;

// Custom AVIO context manager for in-memory buffering of HLS outputs
class AvioMemorySink {
public:
    AvioMemorySink();
    ~AvioMemorySink();

    // Set the callback invoked when a file is closed
    void setFileCallback(HlsFileCallback cb);

    // Install custom io_open/io_close callbacks into the AVFormatContext
    // Must be called before avformat_write_header
    void installIOCallbacks(AVFormatContext* fmtCtx);

private:
    struct FileBuffer {
        std::vector<uint8_t> data;
        std::string filename;
        uint64_t sequence = 0;
        size_t position = 0; // current write/seek position
    };

    // Static trampolines for FFmpeg callbacks
    static int ioOpenCallback(AVFormatContext* s, AVIOContext** pb,
                              const char* url, int flags, AVDictionary** options);
    static int ioCloseCallback(AVFormatContext* s, AVIOContext* pb);

    // Instance methods
    int openFile(AVFormatContext* s, AVIOContext** pb, const char* url, int flags);
    int closeFile(AVFormatContext* s, AVIOContext* pb);

    // AVIO write callback
    static int writePacket(void* opaque, uint8_t* buf, int buf_size);
    // AVIO seek callback
    static int64_t seek(void* opaque, int64_t offset, int whence);

private:
    HlsFileCallback fileCallback;
    std::mutex mtx;
    // Map AVIOContext* -> FileBuffer for active files
    std::unordered_map<AVIOContext*, std::shared_ptr<FileBuffer>> activeBuffers;
    uint64_t segmentSequence = 0;
};
