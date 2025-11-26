#include "AvioMemorySink.h"

#include <algorithm>
#include <string_view>

#include "util/Logger.h"

namespace {
constexpr int kAvioBufferSize = 4096;
constexpr size_t kFileReserveSize = 1 * 1024 * 1024;  // 1 MB
constexpr std::string_view kTmpSuffix = ".m3u8.tmp";
constexpr std::string_view kM3u8Suffix = ".m3u8";
constexpr std::string_view kM4sSuffix = ".m4s";
constexpr std::string_view kEndlistTag = "#EXT-X-ENDLIST";
}  // namespace

AvioMemorySink::AvioMemorySink() = default;
AvioMemorySink::~AvioMemorySink() = default;

void AvioMemorySink::SetFileCallback(const HlsFileCallback& cb) {
  std::lock_guard<std::mutex> lock(mtx_);
  fileCallback_ = cb;
}

void AvioMemorySink::InstallIoCallbacks(AVFormatContext* fmt_ctx) {
  if (!fmt_ctx) {
    return;
  }
  fmt_ctx->io_open = AvioMemorySink::IoOpenCallback;
  fmt_ctx->io_close2 = AvioMemorySink::IoCloseCallback;
  fmt_ctx->opaque = this;
}

int AvioMemorySink::IoOpenCallback(AVFormatContext* s, AVIOContext** pb, const char* url, int flags,
                                   AVDictionary** options) {
  if (!s || !s->opaque) {
    return AVERROR(EINVAL);
  }
  auto* sink = static_cast<AvioMemorySink*>(s->opaque);
  return sink->OpenFile(s, pb, url, flags);
}

int AvioMemorySink::IoCloseCallback(AVFormatContext* s, AVIOContext* pb) {
  if (!s || !s->opaque || !pb) {
    return AVERROR(EINVAL);
  }
  auto* sink = static_cast<AvioMemorySink*>(s->opaque);
  return sink->CloseFile(s, pb);
}

int AvioMemorySink::WritePacket(void* opaque, uint8_t* buf, int buf_size) {
  if (!opaque || !buf || buf_size <= 0) {
    return AVERROR(EINVAL);
  }

  auto* fb = static_cast<FileBuffer*>(opaque);
  const size_t new_size = fb->position + static_cast<size_t>(buf_size);

  if (new_size > fb->data.size()) {
    fb->data.resize(new_size);
  }

  // Modern C++: use std::copy instead of memcpy
  std::copy(buf, buf + buf_size, fb->data.begin() + fb->position);
  fb->position = new_size;

  return buf_size;
}

int64_t AvioMemorySink::Seek(void* opaque, int64_t offset, int whence) {
  if (!opaque) {
    return AVERROR(EINVAL);
  }

  auto* fb = static_cast<FileBuffer*>(opaque);

  if (whence == AVSEEK_SIZE) {
    return static_cast<int64_t>(fb->data.size());
  }

  int64_t new_pos = 0;
  switch (whence) {
    case SEEK_SET:
      new_pos = offset;
      break;
    case SEEK_CUR:
      new_pos = static_cast<int64_t>(fb->position) + offset;
      break;
    case SEEK_END:
      new_pos = static_cast<int64_t>(fb->data.size()) + offset;
      break;
    default:
      return AVERROR(EINVAL);
  }

  if (new_pos < 0) {
    return AVERROR(EINVAL);
  }

  fb->position = static_cast<size_t>(new_pos);

  // Grow buffer if seeking beyond current size
  if (fb->position > fb->data.size()) {
    fb->data.resize(fb->position);
  }

  return new_pos;
}

int AvioMemorySink::OpenFile(AVFormatContext* s, AVIOContext** pb, const char* url, int flags) {
  if (!url || !pb) {
    return AVERROR(EINVAL);
  }

  auto fb = std::make_shared<FileBuffer>();
  fb->filename = url;
  fb->data.reserve(kFileReserveSize);
  fb->position = 0;

  // Assign sequence number for segment files
  const std::string_view filename_view(url);
  if (filename_view.find(kM4sSuffix) != std::string_view::npos) {
    std::lock_guard<std::mutex> lock(mtx_);
    fb->sequence = ++segmentSequence_;
  }

  auto* buffer = static_cast<uint8_t*>(av_malloc(kAvioBufferSize));
  if (!buffer) {
    return AVERROR(ENOMEM);
  }

  AVIOContext* avio_ctx = avio_alloc_context(buffer, kAvioBufferSize, 1, fb.get(), nullptr,
                                             AvioMemorySink::WritePacket, AvioMemorySink::Seek);
  if (!avio_ctx) {
    av_free(buffer);
    return AVERROR(ENOMEM);
  }

  avio_ctx->seekable = AVIO_SEEKABLE_NORMAL;

  {
    std::lock_guard<std::mutex> lock(mtx_);
    activeBuffers_[avio_ctx] = fb;
  }

  *pb = avio_ctx;
  return 0;
}

int AvioMemorySink::CloseFile(AVFormatContext* s, AVIOContext* pb) {
  if (!pb) {
    return 0;
  }

  std::shared_ptr<FileBuffer> fb;
  HlsFileCallback cb;
  {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = activeBuffers_.find(pb);
    if (it != activeBuffers_.end()) {
      fb = it->second;
      activeBuffers_.erase(it);
    }
    cb = fileCallback_;
  }

  // Flush any pending writes
  avio_flush(pb);

  // Free AVIO context (also frees its internal buffer)
  avio_context_free(&pb);

  // Process and callback with file data
  if (fb && cb && !fb->data.empty()) {
    const std::string_view filename_view(fb->filename);
    const bool is_playlist = filename_view.find(kM3u8Suffix) != std::string_view::npos;

    std::string normalized_name = NormalizePlaylistFilename(fb->filename);

    if (is_playlist) {
      ProcessPlaylistFile(normalized_name, fb->data, fb->sequence, cb);
    } else {
      // For non-playlist files (segments), send full content
      cb(normalized_name.c_str(), fb->data.data(), fb->data.size(), 0, fb->sequence);
    }
  }

  return 0;
}

void AvioMemorySink::EmitEndlistIfMissing() {
  HlsFileCallback cb;
  std::string name;
  std::vector<uint8_t> data;
  {
    std::lock_guard<std::mutex> lock(mtx_);
    cb = fileCallback_;
    name = lastPlaylistName_;
    data = lastPlaylistData_;
  }

  if (!cb || name.empty() || data.empty()) {
    return;
  }

  // Check if ENDLIST already present
  const std::string_view snapshot(reinterpret_cast<const char*>(data.data()), data.size());
  if (snapshot.find(kEndlistTag) != std::string_view::npos) {
    return;
  }

  // Append ENDLIST with trailing newline
  constexpr std::string_view endlist_suffix = "\n#EXT-X-ENDLIST\n";
  std::vector<uint8_t> with_endlist = data;
  with_endlist.insert(with_endlist.end(), endlist_suffix.begin(), endlist_suffix.end());

  cb(name.c_str(), with_endlist.data(), with_endlist.size(), 1, 0);
}

std::string AvioMemorySink::NormalizePlaylistFilename(const std::string& filename) const {
  std::string_view view(filename);

  // Normalize playlist temp name to final name: *.m3u8.tmp -> *.m3u8
  if (view.size() > kTmpSuffix.size() &&
      view.substr(view.size() - kTmpSuffix.size()) == kTmpSuffix) {
    return std::string(view.substr(0, view.size() - kTmpSuffix.size())) + std::string(kM3u8Suffix);
  }

  return filename;
}

void AvioMemorySink::ProcessPlaylistFile(const std::string& filename,
                                         const std::vector<uint8_t>& data, uint64_t sequence,
                                         const HlsFileCallback& callback) {
  // Update cache under lock BEFORE calling callback to prevent deadlock
  {
    std::lock_guard<std::mutex> lock(mtx_);
    lastPlaylistName_ = filename;
    lastPlaylistData_ = data;
  }

  callback(filename.c_str(), data.data(), data.size(), 1, sequence);
}
