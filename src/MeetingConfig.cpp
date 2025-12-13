#include "MeetingConfig.h"

MeetingConfig::MeetingConfig(const std::string& meeting_id, const std::string& password,
                             const std::string& display_name, bool is_meeting_start,
                             const std::string& join_token, bool use_raw_audio, bool use_raw_video)
    : meeting_id_(meeting_id),
      password_(password),
      display_name_(display_name),
      is_meeting_start_(is_meeting_start),
      join_token_(join_token),
      use_raw_audio_(use_raw_audio),
      use_raw_video_(use_raw_video) {}
