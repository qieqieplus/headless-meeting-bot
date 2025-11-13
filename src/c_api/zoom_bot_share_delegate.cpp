#include "zoom_bot_share_delegate.h"

#include "MediaController.h"

void ZoomBotShareRendererDelegate::onRendererBeDestroyed() {
  // Renderer is being destroyed, cleanup if needed
}

void ZoomBotShareRendererDelegate::onRawDataFrameReceived(YUVRawDataI420* data) {
  if (!data || !media_controller_) {
    return;
  }

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
    // Push into encoding pipeline with share data type
    media_controller_->PushVideoI420ForSource(StreamKind::kShare, sourceId, yBuffer, uBuffer,
                                              vBuffer, width, height, timestamp);
  }
}

void ZoomBotShareRendererDelegate::onRawDataStatusChanged(RawDataStatus status) {
  // Status changed - no logging needed
}
