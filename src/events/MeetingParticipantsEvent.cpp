#include "MeetingParticipantsEvent.h"

#include "IUserEventSink.h"
#include "zoom_sdk_def.h"

MeetingParticipantsEvent::MeetingParticipantsEvent(IUserEventSink& sink) : sink_(sink) {}

void MeetingParticipantsEvent::onUserJoin(ZOOMSDK::IList<unsigned int>* lst_user_id,
                                          const zchar_t*) {
  if (!lst_user_id) {
    return;
  }

  for (int i = 0; i < lst_user_id->GetCount(); ++i) {
    sink_.HandleParticipantJoin(lst_user_id->GetItem(i));
  }
}

void MeetingParticipantsEvent::onUserLeft(ZOOMSDK::IList<unsigned int>* lst_user_id,
                                          const zchar_t*) {
  if (!lst_user_id) {
    return;
  }

  for (int i = 0; i < lst_user_id->GetCount(); ++i) {
    sink_.HandleParticipantLeft(lst_user_id->GetItem(i));
  }
}

void MeetingParticipantsEvent::onHostChangeNotification(unsigned int) {}

void MeetingParticipantsEvent::onLowOrRaiseHandStatusChanged(bool, unsigned int) {}

void MeetingParticipantsEvent::onUserNamesChanged(ZOOMSDK::IList<unsigned int>*) {}

void MeetingParticipantsEvent::onCoHostChangeNotification(unsigned int, bool) {}

void MeetingParticipantsEvent::onInvalidReclaimHostkey() {}

void MeetingParticipantsEvent::onAllHandsLowered() {}

void MeetingParticipantsEvent::onLocalRecordingStatusChanged(unsigned int,
                                                             ZOOMSDK::RecordingStatus) {}

void MeetingParticipantsEvent::onAllowParticipantsRenameNotification(bool) {}

void MeetingParticipantsEvent::onAllowParticipantsUnmuteSelfNotification(bool) {}

void MeetingParticipantsEvent::onAllowParticipantsStartVideoNotification(bool) {}

void MeetingParticipantsEvent::onAllowParticipantsShareWhiteBoardNotification(bool) {}

void MeetingParticipantsEvent::onRequestLocalRecordingPrivilegeChanged(
    ZOOMSDK::LocalRecordingRequestPrivilegeStatus) {}

void MeetingParticipantsEvent::onAllowParticipantsRequestCloudRecording(bool) {}

void MeetingParticipantsEvent::onInMeetingUserAvatarPathUpdated(unsigned int) {}

void MeetingParticipantsEvent::onParticipantProfilePictureStatusChange(bool) {}

void MeetingParticipantsEvent::onFocusModeStateChanged(bool) {}

void MeetingParticipantsEvent::onFocusModeShareTypeChanged(ZOOMSDK::FocusModeShareType) {}

void MeetingParticipantsEvent::onBotAuthorizerRelationChanged(unsigned int) {}

void MeetingParticipantsEvent::onVirtualNameTagStatusChanged(bool, unsigned int) {}

void MeetingParticipantsEvent::onVirtualNameTagRosterInfoUpdated(unsigned int) {}

void MeetingParticipantsEvent::onGrantCoOwnerPrivilegeChanged(bool) {}

#if defined(WIN32)
void MeetingParticipantsEvent::onCreateCompanionRelation(unsigned int, unsigned int) {}

void MeetingParticipantsEvent::onRemoveCompanionRelation(unsigned int) {}
#endif
