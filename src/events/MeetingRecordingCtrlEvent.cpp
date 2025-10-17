#include "MeetingRecordingCtrlEvent.h"

MeetingRecordingCtrlEvent::MeetingRecordingCtrlEvent(std::function<void(bool)> onPrivilegeChanged) : m_onRecordingPrivilegeChanged(onPrivilegeChanged) {
}


MeetingRecordingCtrlEvent::~MeetingRecordingCtrlEvent() {}

void MeetingRecordingCtrlEvent::onRecordPrivilegeChanged(bool bCanRec) {
    if (m_onRecordingPrivilegeChanged)
        m_onRecordingPrivilegeChanged(bCanRec);
}

void MeetingRecordingCtrlEvent::onRecordingStatus(ZOOMSDK::RecordingStatus status){}

void MeetingRecordingCtrlEvent::onCloudRecordingStatus(ZOOMSDK::RecordingStatus status) {}

void MeetingRecordingCtrlEvent::onLocalRecordingPrivilegeRequestStatus(ZOOMSDK::RequestLocalRecordingStatus status) {}

void MeetingRecordingCtrlEvent::onLocalRecordingPrivilegeRequested(ZOOMSDK::IRequestLocalRecordingPrivilegeHandler* handler) {}

void MeetingRecordingCtrlEvent::onRequestCloudRecordingResponse(ZOOMSDK::RequestStartCloudRecordingStatus status) {}

void MeetingRecordingCtrlEvent::onStartCloudRecordingRequested(ZOOMSDK::IRequestStartCloudRecordingHandler *handler) {}

void MeetingRecordingCtrlEvent::onCloudRecordingStorageFull(time_t gracePeriodDate) {}

void MeetingRecordingCtrlEvent::onEnableAndStartSmartRecordingRequested(ZOOMSDK::IRequestEnableAndStartSmartRecordingHandler *handler) {}

void MeetingRecordingCtrlEvent::onSmartRecordingEnableActionCallback(ZOOMSDK::ISmartRecordingEnableActionHandler *handler) {}

void MeetingRecordingCtrlEvent::onTranscodingStatusChanged(ZOOMSDK::TranscodingStatus status, const zchar_t *path) {}


