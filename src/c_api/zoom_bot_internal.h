#ifndef ZOOM_BOT_INTERNAL_H
#define ZOOM_BOT_INTERNAL_H

#include "zoom_bot_c.h"

// Forward declarations for internal use
struct Meeting;
class ZoomSDK;

namespace Impl {

// Create/destroy/query handle sets (opaque pointers)
ZoomBotHandle CreateSDKHandle(ZoomSDK* sdk_ptr);
void DestroySDKHandle(ZoomBotHandle sdk_handle);
ZoomSDK* GetSDKFromHandle(ZoomBotHandle sdk_handle);

MeetingHandle CreateMeetingHandle(Meeting* meeting_ptr);
void DestroyMeetingHandle(MeetingHandle meeting_handle);
Meeting* GetMeetingFromHandle(MeetingHandle handle);

}  // namespace Impl

#endif  // ZOOM_BOT_INTERNAL_H
