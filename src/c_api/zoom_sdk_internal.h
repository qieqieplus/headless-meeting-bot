#ifndef ZOOM_SDK_INTERNAL_H
#define ZOOM_SDK_INTERNAL_H

#include "zoom_sdk_c.h"

// Forward declarations for internal use
struct Meeting;

namespace Impl {

// Create/destroy/query handle sets (opaque pointers)
ZoomSDKHandle createSdkHandle(ZoomSDKHandle sdk_ptr);
void destroySdkHandle(ZoomSDKHandle sdk_handle);
ZoomSDKHandle getSdkFromHandle(ZoomSDKHandle sdk_handle);

MeetingHandle createMeetingHandle(MeetingHandle meeting_ptr);
void destroyMeetingHandle(MeetingHandle meeting_handle);

// Get Meeting pointer from handle (used internally by dispatch functions)
Meeting *getMeetingFromHandle(MeetingHandle handle);

/**
 * Internal dispatch functions used by delegates - not part of public API
 * These functions are called by the audio and video delegates to forward
 * raw data from Zoom SDK to the registered callbacks or encoding pipeline.
 */

/**
 * Dispatch video data to encoding pipeline
 * Called internally by ZoomSDKVideoRendererDelegate
 * This function always pushes to the encoding pipeline (no raw fallback)
 */
void zoom_meeting_dispatch_video(MeetingHandle meeting_handle,
                                 const char *y_buffer, const char *u_buffer,
                                 const char *v_buffer, unsigned int width,
                                 unsigned int height, unsigned int buffer_len,
                                 unsigned int source_id,
                                 unsigned long long timestamp);

/**
 * Dispatch mixed audio data to MediaController for both raw callback and HLS
 * pipeline Called internally by ZoomSDKAudioRawDataDelegate
 */
void zoom_meeting_dispatch_mixed_audio(MeetingHandle meeting_handle,
                                       const void *pcm_data,
                                       unsigned int length,
                                       unsigned int sample_rate,
                                       unsigned int channels,
                                       unsigned long long timestamp);

/**
 * Dispatch one-way audio data to MediaController for raw callback only
 * Called internally by ZoomSDKAudioRawDataDelegate
 */
void zoom_meeting_dispatch_one_way_audio(
    MeetingHandle meeting_handle, const void *pcm_data, unsigned int length,
    unsigned int sample_rate, unsigned int channels, unsigned int user_id,
    unsigned long long timestamp);

} // namespace Impl

#endif // ZOOM_SDK_INTERNAL_H
