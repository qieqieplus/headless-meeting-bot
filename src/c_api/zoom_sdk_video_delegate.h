#ifndef ZOOM_SDK_VIDEO_DELEGATE_H
#define ZOOM_SDK_VIDEO_DELEGATE_H

#include <iostream>
#include "zoom_sdk_raw_data_def.h"
#include "rawdata/rawdata_renderer_interface.h"
#include "zoom_sdk_c.h"

using namespace std;
using namespace ZOOMSDK;

class ZoomSDKVideoRendererDelegate : public IZoomSDKRendererDelegate {
public:
    explicit ZoomSDKVideoRendererDelegate(MeetingHandle meetingHandle)
        : m_meetingHandle(meetingHandle) {}

    void onRendererBeDestroyed() override;
    void onRawDataFrameReceived(YUVRawDataI420* data) override;
    void onRawDataStatusChanged(RawDataStatus status) override;

private:
    MeetingHandle m_meetingHandle;
};

#endif // ZOOM_SDK_VIDEO_DELEGATE_H

