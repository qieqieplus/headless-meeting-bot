#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

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
using HlsFileCallback = std::function<void(const char* filename, const uint8_t* data, size_t size,
                                           int is_playlist, uint64_t sequence)>;

// Custom AVIO context manager for in-memory buffering of HLS outputs
class AvioMemorySink {
 public:
  AvioMemorySink();
  ~AvioMemorySink();

  // Set the callback invoked when a file is closed
  void SetFileCallback(const HlsFileCallback& cb);

  // Install custom io_open/io_close callbacks into the AVFormatContext
  // Must be called before avformat_write_header
  void InstallIoCallbacks(AVFormatContext* fmt_ctx);

  // Emit a final playlist with #EXT-X-ENDLIST appended if it was missing.
  // Safe to call multiple times; only emits when needed.
  void EmitEndlistIfMissing();

 private:
  struct FileBuffer {
    std::vector<uint8_t> data;
    std::string filename;
    uint64_t sequence = 0;
    size_t position = 0;  // current write/seek position
  };

  // Static trampolines for FFmpeg callbacks
  static int IoOpenCallback(AVFormatContext* s, AVIOContext** pb, const char* url, int flags,
                            AVDictionary** options);
  static int IoCloseCallback(AVFormatContext* s, AVIOContext* pb);

  // Instance methods
  int OpenFile(AVFormatContext* s, AVIOContext** pb, const char* url, int flags);
  int CloseFile(AVFormatContext* s, AVIOContext* pb);

  // AVIO write callback
  static int WritePacket(void* opaque, uint8_t* buf, int buf_size);
  // AVIO seek callback
  static int64_t Seek(void* opaque, int64_t offset, int whence);

 private:
  HlsFileCallback fileCallback_;
  std::mutex mtx_;
  // Map AVIOContext* -> FileBuffer for active files
  std::unordered_map<AVIOContext*, std::shared_ptr<FileBuffer>> activeBuffers_;
  uint64_t segmentSequence_ = 0;

  // Last seen playlist snapshot (normalized name, exact contents)
  std::string lastPlaylistName_;
  std::vector<uint8_t> lastPlaylistData_;
};
