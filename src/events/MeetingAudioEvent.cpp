#include "MeetingAudioEvent.h"

#include <unordered_set>
#include <vector>

#include "IUserEventSink.h"
#include "util/Logger.h"

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

void MeetingAudioEvent::onUserActiveAudioChange(ZOOMSDK::IList<unsigned int>* plst_active_audio) {}

void MeetingAudioEvent::onHostRequestStartAudio(ZOOMSDK::IRequestStartAudioHandler*) {}

void MeetingAudioEvent::onJoin3rdPartyTelephonyAudio(const zchar_t*) {}

void MeetingAudioEvent::onMuteOnEntryStatusChange(bool) {}
