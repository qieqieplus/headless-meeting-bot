#include "AvioMemorySink.h"

#include <algorithm>
#include <cstring>

#include "util/Logger.h"

namespace {
constexpr int kAvioBufferSize = 4096;
constexpr size_t kFileReserveSize = 1 * 1024 * 1024;  // 1 MB
}  // namespace

AvioMemorySink::AvioMemorySink() = default;
AvioMemorySink::~AvioMemorySink() = default;

void AvioMemorySink::SetFileCallback(const HlsFileCallback& cb) {
  std::lock_guard<std::mutex> Lock(mtx_);
  fileCallback_ = std::move(cb);
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
  if (!opaque || !buf || buf_size < 0) {
    return AVERROR(EINVAL);
  }
  auto* fb = static_cast<FileBuffer*>(opaque);

  size_t write_pos = fb->position;
  size_t required = write_pos + static_cast<size_t>(buf_size);
  if (required > fb->data.size()) {
    fb->data.resize(required);
  }
  std::memcpy(fb->data.data() + write_pos, buf, buf_size);
  fb->position += static_cast<size_t>(buf_size);
  return buf_size;
}

int64_t AvioMemorySink::Seek(void* opaque, int64_t offset, int whence) {
  auto* fb = static_cast<FileBuffer*>(opaque);
  if (!fb) {
    return AVERROR(EINVAL);
  }

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
  // Optionally grow buffer when seeking forward beyond end; writer will fill
  // later
  if (fb->position > fb->data.size()) {
    fb->data.resize(fb->position);
  }
  return static_cast<int64_t>(fb->position);
}

int AvioMemorySink::OpenFile(AVFormatContext* s, AVIOContext** pb, const char* url, int flags) {
  if (!url || !pb) {
    return AVERROR(EINVAL);
  }

  std::string filename(url);
  // Strip any path prefix; keep only the basename
  size_t last_slash = filename.find_last_of("/\\");
  if (last_slash != std::string::npos) {
    filename = filename.substr(last_slash + 1);
  }

  auto fb = std::make_shared<FileBuffer>();
  fb->filename = filename;
  fb->data.reserve(kFileReserveSize);  // 1MB initial reserve
  fb->position = 0;

  // Assign sequence number for segment files
  if (filename.find(".m4s") != std::string::npos) {
    std::lock_guard<std::mutex> Lock(mtx_);
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
    std::lock_guard<std::mutex> Lock(mtx_);
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
    std::lock_guard<std::mutex> Lock(mtx_);
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

  // Invoke callback if we have a valid buffer
  if (fb && cb && !fb->data.empty()) {
    const std::string tmp_suffix = ".m3u8.tmp";
    const std::string final_suffix = ".m3u8";
    bool is_playlist = (fb->filename.find(final_suffix) != std::string::npos);
    std::string out_name = fb->filename;
    // Normalize playlist temp name to final name: *.m3u8.tmp -> *.m3u8
    if (is_playlist) {
      if (out_name.size() > tmp_suffix.size() &&
          out_name.rfind(tmp_suffix) == out_name.length() - tmp_suffix.length()) {
        out_name.replace(out_name.length() - tmp_suffix.length(), tmp_suffix.length(),
                         final_suffix);
      }
    }
    cb(out_name.c_str(), fb->data.data(), fb->data.size(), is_playlist ? 1 : 0, fb->sequence);

    // Cache last playlist snapshot for potential ENDLIST emission
    if (is_playlist) {
      std::lock_guard<std::mutex> Lock(mtx_);
      lastPlaylistName_ = out_name;
      lastPlaylistData_.assign(fb->data.begin(), fb->data.end());
    }
  }

  return 0;
}

void AvioMemorySink::EmitEndlistIfMissing() {
  HlsFileCallback cb;
  std::string name;
  std::vector<uint8_t> data;
  {
    std::lock_guard<std::mutex> Lock(mtx_);
    cb = fileCallback_;
    name = lastPlaylistName_;
    data = lastPlaylistData_;
  }
  if (!cb || name.empty() || data.empty()) {
    return;
  }
  // Check if ENDLIST already present
  static const char* kEndlist = "#EXT-X-ENDLIST";
  const std::string snapshot(reinterpret_cast<const char*>(data.data()), data.size());
  if (snapshot.find(kEndlist) != std::string::npos) {
    return;
  }
  // Append ENDLIST with trailing newline
  const char* suffix = "\n#EXT-X-ENDLIST\n";
  std::vector<uint8_t> with_endlist = data;
  with_endlist.insert(with_endlist.end(), suffix, suffix + std::strlen(suffix));
  cb(name.c_str(), with_endlist.data(), with_endlist.size(), /*is_playlist=*/1, /*sequence=*/0);
}
