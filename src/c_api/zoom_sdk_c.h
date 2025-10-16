#ifndef ZOOM_SDK_C_API_H
#define ZOOM_SDK_C_API_H

#ifdef __cplusplus
extern "C" {
#endif

// Return codes for the C API
typedef enum {
    ZOOM_SDK_SUCCESS = 0,
    ZOOM_SDK_ERROR = -1
} ZoomSDKResult;

// Opaque handles for C API
typedef void* ZoomSDKHandle;
typedef void* MeetingHandle;

// Audio callback function type
typedef void (*OnAudioDataReceivedCallback)(MeetingHandle meeting_handle, const void* data, int length, int type, unsigned int node_id);

// Video callback function type
typedef void (*OnVideoDataReceivedCallback)(MeetingHandle meeting_handle, 
                                            const char* y_buffer, const char* u_buffer, const char* v_buffer,
                                            unsigned int width, unsigned int height, 
                                            unsigned int buffer_len, unsigned int source_id,
                                            unsigned long long timestamp);

// Audio type constants
#define ZOOM_AUDIO_TYPE_MIXED 0
#define ZOOM_AUDIO_TYPE_ONE_WAY 1
#define ZOOM_AUDIO_TYPE_SHARE 2

// Meeting status constants (matches Zoom SDK MeetingStatus enum)
typedef enum {
    ZOOM_MEETING_STATUS_IDLE = 0,
    ZOOM_MEETING_STATUS_CONNECTING = 1,
    ZOOM_MEETING_STATUS_WAITINGFORHOST = 2,
    ZOOM_MEETING_STATUS_INMEETING = 3,
    ZOOM_MEETING_STATUS_DISCONNECTING = 4,
    ZOOM_MEETING_STATUS_RECONNECTING = 5,
    ZOOM_MEETING_STATUS_FAILED = 6,
    ZOOM_MEETING_STATUS_ENDED = 7,
    ZOOM_MEETING_STATUS_UNKNOWN = 8,
    ZOOM_MEETING_STATUS_LOCKED = 9,
    ZOOM_MEETING_STATUS_UNLOCKED = 10,
    ZOOM_MEETING_STATUS_IN_WAITING_ROOM = 11,
    ZOOM_MEETING_STATUS_WEBINAR_PROMOTE = 12,
    ZOOM_MEETING_STATUS_WEBINAR_DEPROMOTE = 13,
    ZOOM_MEETING_STATUS_JOIN_BREAKOUT_ROOM = 14,
    ZOOM_MEETING_STATUS_LEAVE_BREAKOUT_ROOM = 15
} ZoomMeetingStatus;

// === SIMPLIFIED API ===

/**
 * Initialize and authenticate the Zoom SDK in one call
 * @param sdk_key The SDK key
 * @param sdk_secret The SDK secret
 * @return ZoomSDKHandle on success, NULL on failure
 */
ZoomSDKHandle zoom_sdk_create(const char* sdk_key, const char* sdk_secret);

/**
 * Cleanup and destroy the SDK
 * @param handle The SDK handle
 */
void zoom_sdk_destroy(ZoomSDKHandle handle);

/**
 * Create and join a meeting in one call
 * @param sdk_handle The SDK handle
 * @param meeting_id The meeting ID to join
 * @param password The meeting password (can be NULL)
 * @param display_name The display name to use in the meeting (can be NULL for default)
 * @param join_token The join token for automatic recording authorization (can be NULL)
 * @param enable_audio 1 to enable raw audio capture, 0 otherwise
 * @param enable_video 1 to enable raw video (shared screen) capture, 0 otherwise
 * @return MeetingHandle on success, NULL on failure
 * @note Video always captures shared screens from other participants, not camera feeds
 */
MeetingHandle zoom_meeting_create_and_join(ZoomSDKHandle sdk_handle,
                                           const char* meeting_id,
                                           const char* password,
                                           const char* display_name,
                                           const char* join_token,
                                           int enable_audio,
                                           int enable_video);

/**
 * Leave and destroy a meeting
 * @param meeting_handle The meeting handle
 */
void zoom_meeting_destroy(MeetingHandle meeting_handle);

/**
 * Get the current meeting status
 * @param meeting_handle The meeting handle
 * @return ZoomMeetingStatus indicating the current status, or ZOOM_MEETING_STATUS_UNKNOWN if handle is invalid
 */
ZoomMeetingStatus zoom_meeting_get_status(MeetingHandle meeting_handle);

/**
 * Set audio callback for receiving raw audio data
 * @param meeting_handle The meeting handle
 * @param callback The audio callback function
 * @return ZoomSDKResult indicating success or failure
 */
ZoomSDKResult zoom_meeting_set_audio_callback(MeetingHandle meeting_handle, OnAudioDataReceivedCallback callback);

/**
 * Set video callback for receiving raw video data
 * @param meeting_handle The meeting handle
 * @param callback The video callback function
 * @return ZoomSDKResult indicating success or failure
 * @note This callback receives shared-screen frames in YUV420 format
 */
ZoomSDKResult zoom_meeting_set_video_callback(MeetingHandle meeting_handle, OnVideoDataReceivedCallback callback);

/**
 * Run the main event loop to process SDK callbacks
 * This function blocks until interrupted (Ctrl+C) or the loop is stopped
 * Call this after setting up your meetings and callbacks
 */
void zoom_sdk_run_loop();

/**
 * Request the main event loop to stop
 * This function is safe to call from another thread. It will wake the loop
 * and cause zoom_sdk_run_loop() to return shortly.
 */
void zoom_sdk_stop_loop();

// Internal functions used by delegates - not part of public API
void zoom_meeting_dispatch_audio(MeetingHandle meeting_handle, const void* data, int length, int type, unsigned int node_id);
void zoom_meeting_dispatch_video(MeetingHandle meeting_handle, 
                                 const char* y_buffer, const char* u_buffer, const char* v_buffer,
                                 unsigned int width, unsigned int height,
                                 unsigned int buffer_len, unsigned int source_id,
                                 unsigned long long timestamp);

#ifdef __cplusplus
}
#endif

#endif // ZOOM_SDK_C_API_H
