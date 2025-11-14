#pragma once

#include <string>

class MeetingConfig {
 private:
  std::string meeting_id_;
  std::string password_;
  bool is_meeting_start_;
  std::string join_token_;
  bool use_raw_audio_;
  bool use_raw_video_;  // Always records share when enabled
  std::string display_name_;

 public:
  explicit MeetingConfig(const std::string& meeting_id = "", const std::string& password = "",
                         const std::string& display_name = "", bool is_meeting_start = false,
                         const std::string& join_token = "", bool use_raw_audio = false,
                         bool use_raw_video = false);

  // Getters
  const std::string& MeetingId() const { return meeting_id_; }
  const std::string& Password() const { return password_; }
  const std::string& DisplayName() const { return display_name_; }
  const std::string& JoinToken() const { return join_token_; }
  bool IsMeetingStart() const { return is_meeting_start_; }
  bool UseRawAudio() const { return use_raw_audio_; }
  bool UseRawVideo() const { return use_raw_video_; }
  bool UseRawRecording() const { return use_raw_audio_ || use_raw_video_; }
};
