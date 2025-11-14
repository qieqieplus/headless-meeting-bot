#pragma once

#include <functional>

#include "meeting_service_components/meeting_sharing_interface.h"

class MeetingShareEvent : public ZOOMSDK::IMeetingShareCtrlEvent {
  std::function<void(const ZOOMSDK::ZoomSDKSharingSourceInfo&)> on_share_start_;
  std::function<void(const ZOOMSDK::ZoomSDKSharingSourceInfo&)> on_share_end_;

 public:
  MeetingShareEvent(std::function<void(const ZOOMSDK::ZoomSDKSharingSourceInfo&)> on_share_start,
                    std::function<void(const ZOOMSDK::ZoomSDKSharingSourceInfo&)> on_share_end);

  // IMeetingShareCtrlEvent implementation
  void onSharingStatus(ZOOMSDK::ZoomSDKSharingSourceInfo share_info) override;
  void onFailedToStartShare() override;
  void onLockShareStatus(bool b_locked) override;
  void onShareContentNotification(ZOOMSDK::ZoomSDKSharingSourceInfo share_info) override;
  void onMultiShareSwitchToSingleShareNeedConfirm(
      ZOOMSDK::IShareSwitchMultiToSingleConfirmHandler* handler) override;
  void onShareSettingTypeChangedNotification(ZOOMSDK::ShareSettingType type) override;
  void onSharedVideoEnded() override;
  void onVideoFileSharePlayError(ZOOMSDK::ZoomSDKVideoFileSharePlayError error) override;
  void onOptimizingShareForVideoClipStatusChanged(
      ZOOMSDK::ZoomSDKSharingSourceInfo share_info) override;
};
