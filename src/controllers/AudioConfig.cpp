#include "controllers/AudioConfig.h"

#include <sstream>

AudioConfig::AudioConfig() = default;
AudioConfig::~AudioConfig() = default;

void AudioConfig::SetAudioCallback(AudioCallback cb, const AudioEncodingConfig* encoding_config) {
  std::lock_guard<std::mutex> lock(mtx_);
  audio_callback_ = std::move(cb);
  if (encoding_config) {
    audio_encoding_cfg_ = std::make_unique<AudioEncodingConfig>(*encoding_config);
  } else {
    audio_encoding_cfg_.reset();
  }
}

void AudioConfig::ClearAudioCallback() {
  std::lock_guard<std::mutex> lock(mtx_);
  audio_callback_ = nullptr;
  audio_encoding_cfg_.reset();
}

AudioConfig::AudioCallback AudioConfig::GetAudioCallback() const {
  std::lock_guard<std::mutex> lock(mtx_);
  return audio_callback_;
}

std::unique_ptr<AudioConfig::AudioEncodingConfig> AudioConfig::GetAudioEncodingConfig() const {
  std::lock_guard<std::mutex> lock(mtx_);
  if (!audio_encoding_cfg_) {
    return nullptr;
  }
  return std::make_unique<AudioEncodingConfig>(*audio_encoding_cfg_);
}

std::string AudioConfig::GetAudioFilename(int audio_type, uint32_t user_id, uint64_t timestamp_ms,
                                          const std::string& codec) const {
  // Determine audio type string
  std::string type_str;
  if (audio_type == 0) {
    type_str = "mixed";
  } else if (audio_type == 1) {
    type_str = "oneway";
  } else if (audio_type == 2) {
    type_str = "share";
  } else {
    return "";
  }

  // Determine extension from codec
  const char* ext = "wav";
  if (codec.find("aac") != std::string::npos) {
    ext = "aac";
  } else if (codec.find("mp3") != std::string::npos) {
    ext = "mp3";
  }

  // Format: {user_id}/{type}_{timestamp}.{ext}
  std::ostringstream oss;
  oss << user_id << "_" << type_str << "_" << timestamp_ms << "." << ext;
  return oss.str();
}

std::string AudioConfig::GetStreamType(const AudioStreamKey& key) const {
  std::string type_str;
  switch (key.audio_type) {
    case 0:
      type_str = "mixed";
      break;
    case 1:
      type_str = "user";
      break;
    case 2:
      type_str = "share";
      break;
    default:
      type_str = "unknown";
      break;
  }
  return "audio[" + type_str + ":" + std::to_string(key.user_id) + "]";
}
