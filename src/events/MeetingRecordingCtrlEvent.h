
#ifndef HEADLESS_ZOOM_BOT_MEETINGRECORDCTRLEVENT_H
#define HEADLESS_ZOOM_BOT_MEETINGRECORDCTRLEVENT_H

#include <functional>
#include "meeting_service_components/meeting_recording_interface.h"


class MeetingRecordingCtrlEvent : public ZOOMSDK::IMeetingRecordingCtrlEvent {

    std::function<void(bool)> m_onRecordingPrivilegeChanged;

public:
    MeetingRecordingCtrlEvent(std::function<void(bool)> onPrivilegeChanged);
    ~MeetingRecordingCtrlEvent();

    /**
     * Fires when the status of local recording changes
     * @param status local recording status
     */
    void onRecordingStatus(ZOOMSDK::RecordingStatus status) override;

    /**
     * Fires when the status of cloud recording changes
     * @param status cloud recording status
     */
    void onCloudRecordingStatus(ZOOMSDK::RecordingStatus status) override;

    /**
     * Fires when recording privilege changes
     * @param bCanRec true if the user can record
     */
    void onRecordPrivilegeChanged(bool bCanRec) override;

    /**
     * fires when the local recording privilege changes
     * @param status status of the local recording privliege request
     */
    void onLocalRecordingPrivilegeRequestStatus(ZOOMSDK::RequestLocalRecordingStatus status) override;

    /**
     * Fires when a user requests local recording privilege
     * @param handler data when local recording privilege is requested
     */
    void onLocalRecordingPrivilegeRequested(ZOOMSDK::IRequestLocalRecordingPrivilegeHandler* handler) override;

    void onRequestCloudRecordingResponse(ZOOMSDK::RequestStartCloudRecordingStatus status) override;
    void onStartCloudRecordingRequested(ZOOMSDK::IRequestStartCloudRecordingHandler* handler) override;
    void onCloudRecordingStorageFull(time_t gracePeriodDate) override;
    void onEnableAndStartSmartRecordingRequested(ZOOMSDK::IRequestEnableAndStartSmartRecordingHandler* handler) override;
    void onSmartRecordingEnableActionCallback(ZOOMSDK::ISmartRecordingEnableActionHandler* handler) override;
    void onTranscodingStatusChanged(ZOOMSDK::TranscodingStatus status,const zchar_t* path) override;
};


#endif //HEADLESS_ZOOM_BOT_MEETINGRECORDCTRLEVENT_H
