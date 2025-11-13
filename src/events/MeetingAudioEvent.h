#ifndef HEADLESS_ZOOM_BOT_MEETINGAUDIOEVENT_H
#define HEADLESS_ZOOM_BOT_MEETINGAUDIOEVENT_H

#include "meeting_service_components/meeting_audio_interface.h"
#include "zoom_sdk_def.h"

class IUserEventSink;

class MeetingAudioEvent : public ZOOMSDK::IMeetingAudioCtrlEvent {
 public:
  explicit MeetingAudioEvent(IUserEventSink& sink);

  void onUserAudioStatusChange(ZOOMSDK::IList<ZOOMSDK::IUserAudioStatus*>* lst_audio_status_change,
                               const zchar_t* str_audio_status_list) override;
  void onUserActiveAudioChange(ZOOMSDK::IList<unsigned int>* plst_active_audio) override;
  void onHostRequestStartAudio(ZOOMSDK::IRequestStartAudioHandler* handler) override;
  void onJoin3rdPartyTelephonyAudio(const zchar_t* audio_info) override;
  void onMuteOnEntryStatusChange(bool b_enabled) override;

 private:
  IUserEventSink& sink_;
};

#endif  // HEADLESS_ZOOM_BOT_MEETINGAUDIOEVENT_H
