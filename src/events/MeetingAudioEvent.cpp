#include "MeetingAudioEvent.h"

#include "IUserEventSink.h"

MeetingAudioEvent::MeetingAudioEvent(IUserEventSink& sink) : sink_(sink) {}

void MeetingAudioEvent::onUserAudioStatusChange(
    ZOOMSDK::IList<ZOOMSDK::IUserAudioStatus*>* lst_audio_status_change, const zchar_t*) {
  if (!lst_audio_status_change) {
    return;
  }

  for (int i = 0; i < lst_audio_status_change->GetCount(); ++i) {
    auto* status = lst_audio_status_change->GetItem(i);
    if (!status) {
      continue;
    }
    sink_.HandleAudioStatus(status->GetUserId(), status->GetStatus());
  }
}

void MeetingAudioEvent::onUserActiveAudioChange(ZOOMSDK::IList<unsigned int>*) {}

void MeetingAudioEvent::onHostRequestStartAudio(ZOOMSDK::IRequestStartAudioHandler*) {}

void MeetingAudioEvent::onJoin3rdPartyTelephonyAudio(const zchar_t*) {}

void MeetingAudioEvent::onMuteOnEntryStatusChange(bool) {}
