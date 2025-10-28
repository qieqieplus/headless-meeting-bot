#include "AvioMemorySink.h"
#include "util/Logger.h"
#include <algorithm>
#include <cstring>

namespace {
constexpr int AVIO_BUFFER_SIZE = 4096;
constexpr size_t FILE_RESERVE_SIZE = 1 * 1024 * 1024; // 1 MB
} // namespace

AvioMemorySink::AvioMemorySink() = default;
AvioMemorySink::~AvioMemorySink() = default;

void AvioMemorySink::setFileCallback(HlsFileCallback cb) {
  std::lock_guard<std::mutex> lock(mtx);
  fileCallback = std::move(cb);
}

void AvioMemorySink::installIOCallbacks(AVFormatContext *fmtCtx) {
  if (!fmtCtx)
    return;
  fmtCtx->io_open = AvioMemorySink::ioOpenCallback;
  fmtCtx->io_close2 = AvioMemorySink::ioCloseCallback;
  fmtCtx->opaque = this;
}

int AvioMemorySink::ioOpenCallback(AVFormatContext *s, AVIOContext **pb,
                                   const char *url, int flags,
                                   AVDictionary **options) {
  if (!s || !s->opaque)
    return AVERROR(EINVAL);
  auto *sink = static_cast<AvioMemorySink *>(s->opaque);
  return sink->openFile(s, pb, url, flags);
}

int AvioMemorySink::ioCloseCallback(AVFormatContext *s, AVIOContext *pb) {
  if (!s || !s->opaque || !pb)
    return AVERROR(EINVAL);
  auto *sink = static_cast<AvioMemorySink *>(s->opaque);
  return sink->closeFile(s, pb);
}

int AvioMemorySink::writePacket(void *opaque, uint8_t *buf, int buf_size) {
  if (!opaque || !buf || buf_size < 0)
    return AVERROR(EINVAL);
  auto *fb = static_cast<FileBuffer *>(opaque);

  size_t writePos = fb->position;
  size_t required = writePos + static_cast<size_t>(buf_size);
  if (required > fb->data.size()) {
    fb->data.resize(required);
  }
  std::memcpy(fb->data.data() + writePos, buf, buf_size);
  fb->position += static_cast<size_t>(buf_size);
  return buf_size;
}

int64_t AvioMemorySink::seek(void *opaque, int64_t offset, int whence) {
  auto *fb = static_cast<FileBuffer *>(opaque);
  if (!fb)
    return AVERROR(EINVAL);

  if (whence == AVSEEK_SIZE) {
    return static_cast<int64_t>(fb->data.size());
  }

  int64_t newPos = 0;
  switch (whence) {
  case SEEK_SET:
    newPos = offset;
    break;
  case SEEK_CUR:
    newPos = static_cast<int64_t>(fb->position) + offset;
    break;
  case SEEK_END:
    newPos = static_cast<int64_t>(fb->data.size()) + offset;
    break;
  default:
    return AVERROR(EINVAL);
  }

  if (newPos < 0)
    return AVERROR(EINVAL);
  fb->position = static_cast<size_t>(newPos);
  // Optionally grow buffer when seeking forward beyond end; writer will fill
  // later
  if (fb->position > fb->data.size()) {
    fb->data.resize(fb->position);
  }
  return static_cast<int64_t>(fb->position);
}

int AvioMemorySink::openFile(AVFormatContext *s, AVIOContext **pb,
                             const char *url, int flags) {
  if (!url || !pb)
    return AVERROR(EINVAL);

  std::string filename(url);
  // Strip any path prefix; keep only the basename
  size_t lastSlash = filename.find_last_of("/\\");
  if (lastSlash != std::string::npos) {
    filename = filename.substr(lastSlash + 1);
  }

  auto fb = std::make_shared<FileBuffer>();
  fb->filename = filename;
  fb->data.reserve(FILE_RESERVE_SIZE); // 1MB initial reserve
  fb->position = 0;

  // Assign sequence number for segment files
  if (filename.find(".m4s") != std::string::npos) {
    std::lock_guard<std::mutex> lock(mtx);
    fb->sequence = ++segmentSequence;
  }

  uint8_t *buffer = static_cast<uint8_t *>(av_malloc(AVIO_BUFFER_SIZE));
  if (!buffer)
    return AVERROR(ENOMEM);

  AVIOContext *avioCtx =
      avio_alloc_context(buffer, AVIO_BUFFER_SIZE, 1, fb.get(), nullptr,
                         AvioMemorySink::writePacket, AvioMemorySink::seek);
  if (!avioCtx) {
    av_free(buffer);
    return AVERROR(ENOMEM);
  }

  avioCtx->seekable = AVIO_SEEKABLE_NORMAL;

  {
    std::lock_guard<std::mutex> lock(mtx);
    activeBuffers[avioCtx] = fb;
  }

  *pb = avioCtx;
  return 0;
}

int AvioMemorySink::closeFile(AVFormatContext *s, AVIOContext *pb) {
  if (!pb)
    return 0;

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
    cb(fb->filename.c_str(), fb->data.data(), fb->data.size(), isPlaylist,
       fb->sequence);
  }

  return 0;
}
