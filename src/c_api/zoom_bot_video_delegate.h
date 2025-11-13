#ifndef ZOOM_BOT_VIDEO_DELEGATE_H
#define ZOOM_BOT_VIDEO_DELEGATE_H

#include "rawdata/rawdata_renderer_interface.h"
#include "zoom_bot_c.h"

// Forward declaration
class MediaController;

class ZoomBotVideoRendererDelegate : public ZOOMSDK::IZoomSDKRendererDelegate {
 public:
  explicit ZoomBotVideoRendererDelegate(MediaController* media_controller)
      : media_controller_(media_controller) {}

  void onRendererBeDestroyed() override;
  void onRawDataFrameReceived(YUVRawDataI420* data) override;
  void onRawDataStatusChanged(RawDataStatus status) override;

 private:
  MediaController* media_controller_;
};

#endif  // ZOOM_BOT_VIDEO_DELEGATE_H
