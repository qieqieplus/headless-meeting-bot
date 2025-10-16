#ifndef HEADLESS_ZOOM_BOT_MEETINGSHAREEVENT_H
#define HEADLESS_ZOOM_BOT_MEETINGSHAREEVENT_H

#include <functional>
#include "meeting_service_components/meeting_sharing_interface.h"

using namespace std;
using namespace ZOOMSDK;

class MeetingShareEvent : public IMeetingShareCtrlEvent {
    function<void(const ZoomSDKSharingSourceInfo&)> m_onShareStart;
    function<void(const ZoomSDKSharingSourceInfo&)> m_onShareEnd;

public:
    MeetingShareEvent(function<void(const ZoomSDKSharingSourceInfo&)> onShareStart,
                      function<void(const ZoomSDKSharingSourceInfo&)> onShareEnd);

    // IMeetingShareCtrlEvent implementation
    void onSharingStatus(ZoomSDKSharingSourceInfo shareInfo) override;
    void onFailedToStartShare() override;
    void onLockShareStatus(bool bLocked) override;
    void onShareContentNotification(ZoomSDKSharingSourceInfo shareInfo) override;
    void onMultiShareSwitchToSingleShareNeedConfirm(IShareSwitchMultiToSingleConfirmHandler* handler_) override;
    void onShareSettingTypeChangedNotification(ShareSettingType type) override;
    void onSharedVideoEnded() override;
    void onVideoFileSharePlayError(ZoomSDKVideoFileSharePlayError error) override;
    void onOptimizingShareForVideoClipStatusChanged(ZoomSDKSharingSourceInfo shareInfo) override;
};

#endif //HEADLESS_ZOOM_BOT_MEETINGSHAREEVENT_H

