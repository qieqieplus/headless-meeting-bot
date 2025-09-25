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

// Audio type constants
#define ZOOM_AUDIO_TYPE_MIXED 0
#define ZOOM_AUDIO_TYPE_ONE_WAY 1
#define ZOOM_AUDIO_TYPE_SHARE 2

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
 * @param enable_audio 1 to enable raw audio capture, 0 otherwise
 * @return MeetingHandle on success, NULL on failure
 */
MeetingHandle zoom_meeting_create_and_join(ZoomSDKHandle sdk_handle,
                                           const char* meeting_id,
                                           const char* password,
                                           const char* display_name,
                                           int enable_audio);

/**
 * Leave and destroy a meeting
 * @param meeting_handle The meeting handle
 */
void zoom_meeting_destroy(MeetingHandle meeting_handle);

/**
 * Set audio callback for receiving raw audio data
 * @param meeting_handle The meeting handle
 * @param callback The audio callback function
 * @return ZoomSDKResult indicating success or failure
 */
ZoomSDKResult zoom_meeting_set_audio_callback(MeetingHandle meeting_handle, OnAudioDataReceivedCallback callback);

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

// Internal function used by audio delegate - not part of public API
void zoom_meeting_dispatch_audio(MeetingHandle meeting_handle, const void* data, int length, int type, unsigned int node_id);

#ifdef __cplusplus
}
#endif

#endif // ZOOM_SDK_C_API_H
