#ifndef ZOOM_SDK_INTERNAL_H
#define ZOOM_SDK_INTERNAL_H

#include "zoom_sdk_c.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Internal dispatch functions used by delegates - not part of public API
 * These functions are called by the audio and video delegates to forward
 * raw data from Zoom SDK to the registered callbacks or encoding pipeline.
 */

/**
 * Dispatch audio data to registered callback
 * Called internally by ZoomSDKAudioRawDataDelegate
 */
void zoom_meeting_dispatch_audio(MeetingHandle meeting_handle,
                                 const void* data,
                                 int length,
                                 int type,
                                 unsigned int node_id);

/**
 * Dispatch video data to encoding pipeline
 * Called internally by ZoomSDKVideoRendererDelegate
 * RENAMED: formerly `zoom_meeting_pipeline_push_video`
 * This function always pushes to the encoding pipeline (no raw fallback)
 */
void zoom_meeting_dispatch_video(MeetingHandle meeting_handle,
                                 const char* y_buffer,
                                 const char* u_buffer,
                                 const char* v_buffer,
                                 unsigned int width,
                                 unsigned int height,
                                 unsigned int buffer_len,
                                 unsigned int source_id,
                                 unsigned long long timestamp);

#ifdef __cplusplus
}
#endif

#endif // ZOOM_SDK_INTERNAL_H
