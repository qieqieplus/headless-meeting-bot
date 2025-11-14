#pragma once

#include "meeting_service_components/meeting_audio_interface.h"
#include "meeting_service_components/meeting_participants_ctrl_interface.h"

class IUserEventSink;

class MeetingParticipantsEvent : public ZOOMSDK::IMeetingParticipantsCtrlEvent {
 public:
  explicit MeetingParticipantsEvent(IUserEventSink& sink);

  void onUserJoin(ZOOMSDK::IList<unsigned int>* lst_user_id, const zchar_t* str_user_list) override;
  void onUserLeft(ZOOMSDK::IList<unsigned int>* lst_user_id, const zchar_t* str_user_list) override;
  void onHostChangeNotification(unsigned int user_id) override;
  void onLowOrRaiseHandStatusChanged(bool b_low, unsigned int userid) override;
  void onUserNamesChanged(ZOOMSDK::IList<unsigned int>* lst_user_id) override;
  void onCoHostChangeNotification(unsigned int user_id, bool is_co_host) override;
  void onInvalidReclaimHostkey() override;
  void onAllHandsLowered() override;
  void onLocalRecordingStatusChanged(unsigned int user_id,
                                     ZOOMSDK::RecordingStatus status) override;
  void onAllowParticipantsRenameNotification(bool b_allow) override;
  void onAllowParticipantsUnmuteSelfNotification(bool b_allow) override;
  void onAllowParticipantsStartVideoNotification(bool b_allow) override;
  void onAllowParticipantsShareWhiteBoardNotification(bool b_allow) override;
  void onRequestLocalRecordingPrivilegeChanged(
      ZOOMSDK::LocalRecordingRequestPrivilegeStatus status) override;
  void onAllowParticipantsRequestCloudRecording(bool b_allow) override;
  void onInMeetingUserAvatarPathUpdated(unsigned int user_id) override;
  void onParticipantProfilePictureStatusChange(bool b_hidden) override;
  void onFocusModeStateChanged(bool b_enabled) override;
  void onFocusModeShareTypeChanged(ZOOMSDK::FocusModeShareType type) override;
  void onBotAuthorizerRelationChanged(unsigned int authorize_user_id) override;
  void onVirtualNameTagStatusChanged(bool b_on, unsigned int user_id) override;
  void onVirtualNameTagRosterInfoUpdated(unsigned int user_id) override;
  void onGrantCoOwnerPrivilegeChanged(bool can_grant_other) override;
#if defined(WIN32)
  void onCreateCompanionRelation(unsigned int parentUserID, unsigned int childUserID) override;
  void onRemoveCompanionRelation(unsigned int childUserID) override;
#endif

 private:
  IUserEventSink& sink_;
};
