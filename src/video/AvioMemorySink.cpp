#include "AvioMemorySink.h"
#include "util/Logger.h"
#include <cstring>
#include <algorithm>

AvioMemorySink::AvioMemorySink() = default;
AvioMemorySink::~AvioMemorySink() = default;

void AvioMemorySink::setFileCallback(HlsFileCallback cb) {
    std::lock_guard<std::mutex> lock(mtx);
    fileCallback = std::move(cb);
}

void AvioMemorySink::installIOCallbacks(AVFormatContext* fmtCtx) {
    if (!fmtCtx) return;
    fmtCtx->io_open = AvioMemorySink::ioOpenCallback;
    fmtCtx->io_close = AvioMemorySink::ioCloseCallback;
    fmtCtx->opaque = this;
}

int AvioMemorySink::ioOpenCallback(AVFormatContext* s, AVIOContext** pb,
                                   const char* url, int flags, AVDictionary** options) {
    if (!s || !s->opaque) return AVERROR(EINVAL);
    auto* sink = static_cast<AvioMemorySink*>(s->opaque);
    return sink->openFile(s, pb, url, flags);
}

int AvioMemorySink::ioCloseCallback(AVFormatContext* s, AVIOContext* pb) {
    if (!s || !s->opaque || !pb) return AVERROR(EINVAL);
    auto* sink = static_cast<AvioMemorySink*>(s->opaque);
    return sink->closeFile(s, pb);
}

int AvioMemorySink::writePacket(void* opaque, uint8_t* buf, int buf_size) {
    if (!opaque || !buf || buf_size < 0) return AVERROR(EINVAL);
    auto* fb = static_cast<FileBuffer*>(opaque);
    size_t oldSize = fb->data.size();
    fb->data.resize(oldSize + buf_size);
    std::memcpy(fb->data.data() + oldSize, buf, buf_size);
    return buf_size;
}

int AvioMemorySink::openFile(AVFormatContext* s, AVIOContext** pb, const char* url, int flags) {
    if (!url || !pb) return AVERROR(EINVAL);

    std::string filename(url);
    // Strip any path prefix; keep only the basename
    size_t lastSlash = filename.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        filename = filename.substr(lastSlash + 1);
    }

    // Ignore .tmp files (FFmpeg temp_file mechanism)
    if (filename.find(".tmp") != std::string::npos) {
        // Return a dummy AVIO context that discards writes
        constexpr int bufferSize = 4096;
        uint8_t* buffer = static_cast<uint8_t*>(av_malloc(bufferSize));
        if (!buffer) return AVERROR(ENOMEM);
        AVIOContext* avioCtx = avio_alloc_context(buffer, bufferSize, 1, nullptr, nullptr,
                                                  [](void*, uint8_t*, int size) { return size; },
                                                  nullptr);
        if (!avioCtx) {
            av_free(buffer);
            return AVERROR(ENOMEM);
        }
        *pb = avioCtx;
        return 0;
    }

    auto fb = std::make_shared<FileBuffer>();
    fb->filename = filename;
    fb->data.reserve(1024 * 1024); // 1MB initial reserve

    // Assign sequence number for segment files
    if (filename.find(".m4s") != std::string::npos) {
        std::lock_guard<std::mutex> lock(mtx);
        fb->sequence = ++segmentSequence;
    }

    constexpr int bufferSize = 4096;
    uint8_t* buffer = static_cast<uint8_t*>(av_malloc(bufferSize));
    if (!buffer) return AVERROR(ENOMEM);

    AVIOContext* avioCtx = avio_alloc_context(buffer, bufferSize, 1, fb.get(),
                                              nullptr, AvioMemorySink::writePacket, nullptr);
    if (!avioCtx) {
        av_free(buffer);
        return AVERROR(ENOMEM);
    }

    {
        std::lock_guard<std::mutex> lock(mtx);
        activeBuffers[avioCtx] = fb;
    }

    *pb = avioCtx;
    return 0;
}

int AvioMemorySink::closeFile(AVFormatContext* s, AVIOContext* pb) {
    if (!pb) return 0;

    std::shared_ptr<FileBuffer> fb;
    HlsFileCallback cb;
    {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = activeBuffers.find(pb);
        if (it != activeBuffers.end()) {
            fb = it->second;
            activeBuffers.erase(it);
        }
        cb = fileCallback;
    }

    // Flush any pending writes
    avio_flush(pb);

    // Free AVIO context
    if (pb->buffer) {
        av_free(pb->buffer);
    }
    avio_context_free(&pb);

    // Invoke callback if we have a valid buffer
    if (fb && cb && !fb->data.empty()) {
        int isPlaylist = (fb->filename.find(".m3u8") != std::string::npos) ? 1 : 0;
        cb(fb->filename.c_str(), fb->data.data(), fb->data.size(), isPlaylist, fb->sequence);
    }

    return 0;
}

