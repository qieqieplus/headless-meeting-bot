#pragma once

#include "meeting_service_components/meeting_audio_interface.h"
#include "meeting_service_components/meeting_video_interface.h"

class IUserEventSink {
 public:
  virtual ~IUserEventSink() = default;

  virtual void HandleParticipantJoin(unsigned int user_id) = 0;
  virtual void HandleParticipantLeft(unsigned int user_id) = 0;
  virtual void HandleAudioStatus(unsigned int user_id, ZOOMSDK::AudioStatus status) = 0;
  virtual void HandleVideoStatus(unsigned int user_id, ZOOMSDK::VideoStatus status) = 0;
  virtual void HandleSpeakingStatus(unsigned int user_id, bool is_speaking) = 0;
};
