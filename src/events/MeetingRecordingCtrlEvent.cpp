#include "MeetingRecordingCtrlEvent.h"

MeetingRecordingCtrlEvent::MeetingRecordingCtrlEvent(std::function<void(bool)> on_privilege_changed)
    : on_recording_privilege_changed_(on_privilege_changed) {}

MeetingRecordingCtrlEvent::~MeetingRecordingCtrlEvent() = default;

void MeetingRecordingCtrlEvent::onRecordPrivilegeChanged(bool b_can_rec) {
  if (on_recording_privilege_changed_) {
    on_recording_privilege_changed_(b_can_rec);
  }
}

void MeetingRecordingCtrlEvent::onRecordingStatus(ZOOMSDK::RecordingStatus status) {}

void MeetingRecordingCtrlEvent::onCloudRecordingStatus(ZOOMSDK::RecordingStatus status) {}

void MeetingRecordingCtrlEvent::onLocalRecordingPrivilegeRequestStatus(
    ZOOMSDK::RequestLocalRecordingStatus status) {}

void MeetingRecordingCtrlEvent::onLocalRecordingPrivilegeRequested(
    ZOOMSDK::IRequestLocalRecordingPrivilegeHandler* handler) {}

void MeetingRecordingCtrlEvent::onRequestCloudRecordingResponse(
    ZOOMSDK::RequestStartCloudRecordingStatus status) {}

void MeetingRecordingCtrlEvent::onStartCloudRecordingRequested(
    ZOOMSDK::IRequestStartCloudRecordingHandler* handler) {}

void MeetingRecordingCtrlEvent::onCloudRecordingStorageFull(time_t grace_period_date) {}

void MeetingRecordingCtrlEvent::onEnableAndStartSmartRecordingRequested(
    ZOOMSDK::IRequestEnableAndStartSmartRecordingHandler* handler) {}

void MeetingRecordingCtrlEvent::onSmartRecordingEnableActionCallback(
    ZOOMSDK::ISmartRecordingEnableActionHandler* handler) {}

void MeetingRecordingCtrlEvent::onTranscodingStatusChanged(ZOOMSDK::TranscodingStatus status,
                                                           const zchar_t* path) {}
