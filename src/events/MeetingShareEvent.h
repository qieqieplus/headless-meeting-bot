#pragma once

#include "meeting_service_components/meeting_sharing_interface.h"

class IUserEventSink;

class MeetingShareEvent : public ZOOMSDK::IMeetingShareCtrlEvent {
 public:
  explicit MeetingShareEvent(IUserEventSink& sink);

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

 private:
  IUserEventSink& sink_;
};
