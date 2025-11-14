#pragma once

#include "rawdata/rawdata_renderer_interface.h"
#include "zoom_bot_c.h"
#include "zoom_sdk_raw_data_def.h"

// Forward declaration
class MediaController;

class ZoomBotShareRendererDelegate : public ZOOMSDK::IZoomSDKRendererDelegate {
 public:
  explicit ZoomBotShareRendererDelegate(MediaController* media_controller)
      : media_controller_(media_controller) {}

  void onRendererBeDestroyed() override;
  void onRawDataFrameReceived(YUVRawDataI420* data) override;
  void onRawDataStatusChanged(RawDataStatus status) override;

 private:
  MediaController* media_controller_;
};
