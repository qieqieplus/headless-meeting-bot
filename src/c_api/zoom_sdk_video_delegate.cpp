#include "zoom_sdk_video_delegate.h"

void ZoomSDKVideoRendererDelegate::onRendererBeDestroyed() {
    // Renderer is being destroyed, cleanup if needed
}

void ZoomSDKVideoRendererDelegate::onRawDataFrameReceived(YUVRawDataI420* data) {
    if (!data) return;
    
    // Get YUV frame data
    char* yBuffer = data->GetYBuffer();
    char* uBuffer = data->GetUBuffer();
    char* vBuffer = data->GetVBuffer();
    unsigned int width = data->GetStreamWidth();
    unsigned int height = data->GetStreamHeight();
    unsigned int bufferLen = data->GetBufferLen();
    unsigned int sourceId = data->GetSourceID();
    unsigned long long timestamp = data->GetTimeStamp();
    
    if (yBuffer && uBuffer && vBuffer && bufferLen > 0) {
        // Dispatch video frame to C API callback
        zoom_meeting_dispatch_video(m_meetingHandle, yBuffer, uBuffer, vBuffer, 
                                    width, height, bufferLen, sourceId, timestamp);
    }
}

void ZoomSDKVideoRendererDelegate::onRawDataStatusChanged(RawDataStatus status) {
    if (status == RawData_On) {
        // Video raw data started
    } else {
        // Video raw data stopped
    }
}

