#include "zoom_bot_video_delegate.h"

#include "MediaController.h"
#include "zoom_sdk_raw_data_def.h"

void ZoomBotVideoRendererDelegate::onRendererBeDestroyed() {
  // Renderer is being destroyed, cleanup if needed
}

void ZoomBotVideoRendererDelegate::onRawDataFrameReceived(YUVRawDataI420* data) {
  if (!data || !media_controller_) return;

  // Get YUV frame data
  char* yBuffer = data->GetYBuffer();
  char* uBuffer = data->GetUBuffer();
  char* vBuffer = data->GetVBuffer();
  unsigned int width = data->GetStreamWidth();
  unsigned int height = data->GetStreamHeight();
  unsigned int bufferLen = data->GetBufferLen();
  unsigned int sourceId = data->GetSourceID();
  uint64_t timestamp = data->GetTimeStamp();

  if (yBuffer && uBuffer && vBuffer && bufferLen > 0) {
    // Push into encoding pipeline with camera data type
    media_controller_->PushVideoI420ForSource(StreamKind::kCamera, sourceId, yBuffer, uBuffer,
                                              vBuffer, width, height, timestamp);
  }
}

void ZoomBotVideoRendererDelegate::onRawDataStatusChanged(RawDataStatus status) {
  // Status changed - no logging needed
}
