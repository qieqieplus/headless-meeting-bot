#ifndef ZOOM_SDK_C_API_H
#define ZOOM_SDK_C_API_H

// Compatibility shim: redirects old zoom_sdk_* names to new zoom_bot_* names
// This file is provided for backward compatibility and may be removed in a future release.

#include "zoom_bot_c.h"

// Type aliases
#define ZoomSDKHandle ZoomBotHandle
#define ZoomSDKResult ZoomBotResult
#define ZOOM_SDK_SUCCESS ZOOM_BOT_SUCCESS
#define ZOOM_SDK_ERROR ZOOM_BOT_ERROR
#define ZOOM_SDK_C_API ZOOM_BOT_C_API

// Function aliases
#define zoom_sdk_create zoom_bot_create
#define zoom_sdk_destroy zoom_bot_destroy
#define zoom_sdk_run_loop zoom_bot_run_loop
#define zoom_sdk_stop_loop zoom_bot_stop_loop
#define zoom_meeting_create_and_join zoom_bot_meeting_create_and_join
#define zoom_meeting_destroy zoom_bot_meeting_destroy
#define zoom_meeting_set_status_callback zoom_bot_meeting_set_status_callback
#define zoom_meeting_set_user_status_callback zoom_bot_meeting_set_user_status_callback
#define zoom_meeting_set_audio_callback zoom_bot_meeting_set_audio_callback
#define zoom_meeting_set_hls_video_callback zoom_bot_meeting_set_hls_video_callback

#endif  // ZOOM_SDK_C_API_H
