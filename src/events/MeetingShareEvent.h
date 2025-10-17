#ifndef HEADLESS_ZOOM_BOT_MEETINGSHAREEVENT_H
#define HEADLESS_ZOOM_BOT_MEETINGSHAREEVENT_H

#include <functional>
#include "meeting_service_components/meeting_sharing_interface.h"


class MeetingShareEvent : public ZOOMSDK::IMeetingShareCtrlEvent {
    std::function<void(const ZOOMSDK::ZoomSDKSharingSourceInfo&)> m_onShareStart;
    std::function<void(const ZOOMSDK::ZoomSDKSharingSourceInfo&)> m_onShareEnd;

public:
    MeetingShareEvent(std::function<void(const ZOOMSDK::ZoomSDKSharingSourceInfo&)> onShareStart,
                      std::function<void(const ZOOMSDK::ZoomSDKSharingSourceInfo&)> onShareEnd);

    // IMeetingShareCtrlEvent implementation
    void onSharingStatus(ZOOMSDK::ZoomSDKSharingSourceInfo shareInfo) override;
    void onFailedToStartShare() override;
    void onLockShareStatus(bool bLocked) override;
    void onShareContentNotification(ZOOMSDK::ZoomSDKSharingSourceInfo shareInfo) override;
    void onMultiShareSwitchToSingleShareNeedConfirm(ZOOMSDK::IShareSwitchMultiToSingleConfirmHandler* handler_) override;
    void onShareSettingTypeChangedNotification(ZOOMSDK::ShareSettingType type) override;
    void onSharedVideoEnded() override;
    void onVideoFileSharePlayError(ZOOMSDK::ZoomSDKVideoFileSharePlayError error) override;
    void onOptimizingShareForVideoClipStatusChanged(ZOOMSDK::ZoomSDKSharingSourceInfo shareInfo) override;
};

#endif //HEADLESS_ZOOM_BOT_MEETINGSHAREEVENT_H

