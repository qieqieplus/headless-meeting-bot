#include "MeetingShareEvent.h"

#include "IUserEventSink.h"
#include "util/Logger.h"

MeetingShareEvent::MeetingShareEvent(IUserEventSink& sink) : sink_(sink) {}

void MeetingShareEvent::onSharingStatus(ZOOMSDK::ZoomSDKSharingSourceInfo share_info) {
  switch (share_info.status) {
    case ZOOMSDK::Sharing_Other_Share_Begin:
      Logger::GetInstance().Info("Share started from user " + std::to_string(share_info.userid));
      sink_.HandleShareStatus(share_info.userid, share_info, true);
      break;
    case ZOOMSDK::Sharing_Other_Share_End:
      Logger::GetInstance().Info("Share ended from user " + std::to_string(share_info.userid));
      sink_.HandleShareStatus(share_info.userid, share_info, false);
      break;
    case ZOOMSDK::Sharing_Self_Send_Begin:
      Logger::GetInstance().Info("Self share started");
      break;
    case ZOOMSDK::Sharing_Self_Send_End:
      Logger::GetInstance().Info("Self share ended");
      break;
    default:
      break;
  }
}

void MeetingShareEvent::onFailedToStartShare() {
  Logger::GetInstance().Error("Failed to start share");
}

void MeetingShareEvent::onLockShareStatus(bool b_locked) {
  // Not implemented
}

void MeetingShareEvent::onShareContentNotification(ZOOMSDK::ZoomSDKSharingSourceInfo share_info) {
  // Not implemented
}

void MeetingShareEvent::onMultiShareSwitchToSingleShareNeedConfirm(
    ZOOMSDK::IShareSwitchMultiToSingleConfirmHandler* handler) {
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

void MeetingShareEvent::onOptimizingShareForVideoClipStatusChanged(
    ZOOMSDK::ZoomSDKSharingSourceInfo share_info) {
  // Not implemented
}
