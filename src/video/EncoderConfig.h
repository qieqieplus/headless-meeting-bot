#pragma once

#include <string>

enum class GOPSize : int {
  kLow = 120,
  kMedium = 240,
  kHigh = 960,
};

enum class BitRate : int {
  kLow = 1000,
  kMedium = 2000,
  kHigh = 3000,
};

struct VideoEncoderConfig {
  // auto-detect
  int width = 0;
  int height = 0;
  int fps = 0;
  int bitrate_kbps = static_cast<int>(BitRate::kMedium);
  // Keyframe interval in frames (not seconds)
  int gop_size = static_cast<int>(GOPSize::kMedium);
  // "x264", "nvenc", "auto"
  std::string encoder = "auto";
  // x264: ultrafast..veryslow; nvenc: fast,medium,slow
  std::string preset = "fast";
  // "baseline", "main", "high"
  std::string profile = "main";
};

struct AudioEncoderConfig {
  int sample_rate = 32000;    // Target sample rate
  int channels = 1;           // Stereo (1 = mono, 2 = stereo)
  int bitrate_kbps = 128;     // AAC bitrate
  std::string codec = "aac";  // "aac" or "libfdk_aac"
};

struct HlsMuxerConfig {
  std::string hls_prefix = "";          // Base name for playlist and segments
  std::string playlist_type = "event";  // "vod", "live", or "event"
  int hls_list_size = 0;                // 0 = keep all segments in playlist
  int hls_time_seconds = 24;            // Target segment duration in seconds
};
