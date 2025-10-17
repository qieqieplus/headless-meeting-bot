#include "MeetingShareEvent.h"
#include "util/Logger.h"

MeetingShareEvent::MeetingShareEvent(std::function<void(const ZOOMSDK::ZoomSDKSharingSourceInfo&)> onShareStart,
                                     std::function<void(const ZOOMSDK::ZoomSDKSharingSourceInfo&)> onShareEnd)
    : m_onShareStart(onShareStart), m_onShareEnd(onShareEnd) {
}

void MeetingShareEvent::onSharingStatus(ZOOMSDK::ZoomSDKSharingSourceInfo shareInfo) {
    switch (shareInfo.status) {
        case ZOOMSDK::Sharing_Other_Share_Begin:
            Util::Logger::getInstance().info("Share started from user " + std::to_string(shareInfo.userid));
            if (m_onShareStart) {
                m_onShareStart(shareInfo);
            }
            break;
        case ZOOMSDK::Sharing_Other_Share_End:
            Util::Logger::getInstance().info("Share ended from user " + std::to_string(shareInfo.userid));
            if (m_onShareEnd) {
                m_onShareEnd(shareInfo);
            }
            break;
        case ZOOMSDK::Sharing_Self_Send_Begin:
            Util::Logger::getInstance().info("Self share started");
            break;
        case ZOOMSDK::Sharing_Self_Send_End:
            Util::Logger::getInstance().info("Self share ended");
            break;
        default:
            break;
    }
}

void MeetingShareEvent::onFailedToStartShare() {
    Util::Logger::getInstance().error("Failed to start share");
}

void MeetingShareEvent::onLockShareStatus(bool bLocked) {
    // Not implemented
}

void MeetingShareEvent::onShareContentNotification(ZOOMSDK::ZoomSDKSharingSourceInfo shareInfo) {
    // Not implemented
}

void MeetingShareEvent::onMultiShareSwitchToSingleShareNeedConfirm(ZOOMSDK::IShareSwitchMultiToSingleConfirmHandler* handler_) {
    // Not implemented
}

void MeetingShareEvent::onShareSettingTypeChangedNotification(ZOOMSDK::ShareSettingType type) {
    // Not implemented
}

void MeetingShareEvent::onSharedVideoEnded() {
    // Not implemented
}

void MeetingShareEvent::onVideoFileSharePlayError(ZOOMSDK::ZoomSDKVideoFileSharePlayError error) {
    // Not implemented
}

void MeetingShareEvent::onOptimizingShareForVideoClipStatusChanged(ZOOMSDK::ZoomSDKSharingSourceInfo shareInfo) {
    // Not implemented
}

