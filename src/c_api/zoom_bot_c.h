#ifndef ZOOM_BOT_C_API_H
#define ZOOM_BOT_C_API_H

#include <stddef.h>
#include <stdint.h>

#if !defined(ZOOM_BOT_C_API)
#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef ZOOM_BOT_C_BUILD
#define ZOOM_BOT_C_API __declspec(dllexport)
#else
#define ZOOM_BOT_C_API __declspec(dllimport)
#endif
#else
#define ZOOM_BOT_C_API __attribute__((visibility("default")))
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Return codes for the C API
typedef enum { ZOOM_BOT_SUCCESS = 0, ZOOM_BOT_ERROR = -1 } ZoomBotResult;

// Opaque handles for C API
typedef void* ZoomBotHandle;
typedef void* MeetingHandle;

// Audio callback function type
typedef void (*OnAudioDataReceivedCallback)(MeetingHandle meeting_handle, const void* data,
                                            int length, int type, unsigned int node_id);

// HLS file callback - called when muxer writes a file (init.mp4, segments,
// playlist)
typedef void (*OnHlsFileCallback)(MeetingHandle meeting_handle, const char* filename,
                                  const unsigned char* data, size_t size, int is_playlist,
                                  uint64_t sequence);

// Audio type constants
#define ZOOM_AUDIO_TYPE_MIXED 0
#define ZOOM_AUDIO_TYPE_ONE_WAY 1
#define ZOOM_AUDIO_TYPE_SHARE 2

// Raw data type constants for video streams
typedef enum {
  ZOOM_RAW_DATA_TYPE_VIDEO = 0,  // Camera video stream
  ZOOM_RAW_DATA_TYPE_SHARE = 1   // Screen share stream
} ZoomRawDataType;

// User status events
typedef enum {
  ZOOM_USER_EVENT_TYPE_SNAPSHOT = 0,
  ZOOM_USER_EVENT_TYPE_JOINED = 1,
  ZOOM_USER_EVENT_TYPE_LEFT = 2,
  ZOOM_USER_EVENT_TYPE_AUDIO_MUTED = 3,
  ZOOM_USER_EVENT_TYPE_AUDIO_UNMUTED = 4,
  ZOOM_USER_EVENT_TYPE_VIDEO_ON = 5,
  ZOOM_USER_EVENT_TYPE_VIDEO_OFF = 6,
  ZOOM_USER_EVENT_TYPE_SHARE_STARTED = 7,
  ZOOM_USER_EVENT_TYPE_SHARE_STOPPED = 8
} ZoomUserEventType;

typedef struct {
  unsigned int id;   // Zoom user id
  const char* name;  // UTF-8 display name (lifetime until callback returns)
  int audio;         // 1 if audio on, else 0
  int video;         // 1 if video on, else 0
  int share;         // 1 if sharing, else 0
} ZoomUserStatus;

typedef struct {
  ZoomUserEventType event;  // What happened
  ZoomUserStatus user;      // Current user state
  uint64_t timestamp_ms;    // Media timeline timestamp for event alignment
} ZoomUserStatusEvent;

typedef void (*OnUserStatusEventCallback)(MeetingHandle meeting_handle,
                                          const ZoomUserStatusEvent* event);

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
 * @return ZoomBotHandle on success, NULL on failure
 */
ZOOM_BOT_C_API ZoomBotHandle zoom_bot_create(const char* sdk_key, const char* sdk_secret);

/**
 * Cleanup and destroy the SDK
 * @param handle The SDK handle
 */
ZOOM_BOT_C_API void zoom_bot_destroy(ZoomBotHandle handle);

/**
 * Create and join a meeting in one call
 * @param sdk_handle The SDK handle
 * @param meeting_id The meeting ID to join
 * @param password The meeting password (can be NULL)
 * @param display_name The display name to use in the meeting (can be NULL for
 * default)
 * @param join_token The join token for automatic recording authorization (can
 * be NULL)
 * @param enable_audio 1 to enable raw audio capture, 0 otherwise
 * @param enable_video 1 to enable raw video (shared screen) capture, 0
 * otherwise
 * @return MeetingHandle on success, NULL on failure
 * @note Video always captures shared screens from other participants, not
 * camera feeds
 */
ZOOM_BOT_C_API MeetingHandle zoom_bot_meeting_create_and_join(
    ZoomBotHandle sdk_handle, const char* meeting_id, const char* password,
    const char* display_name, const char* join_token, int enable_audio, int enable_video);

/**
 * Leave and destroy a meeting
 * @param meeting_handle The meeting handle
 */
ZOOM_BOT_C_API void zoom_bot_meeting_destroy(MeetingHandle meeting_handle);

typedef void (*OnMeetingStatusCallback)(MeetingHandle meeting_handle, ZoomMeetingStatus status,
                                        int detail_code);

/**
 * Register a callback to receive meeting status updates.
 * Passing NULL removes the existing callback.
 */
ZOOM_BOT_C_API ZoomBotResult zoom_bot_meeting_set_status_callback(MeetingHandle meeting_handle,
                                                                  OnMeetingStatusCallback callback);

/**
 * Register a callback to receive user status updates
 * (join/leave/audio/video/share). Passing NULL removes the existing callback.
 */
ZOOM_BOT_C_API ZoomBotResult zoom_bot_meeting_set_user_status_callback(
    MeetingHandle meeting_handle, OnUserStatusEventCallback callback);

/**
 * Set audio callback for receiving raw audio data
 * @param meeting_handle The meeting handle
 * @param callback The audio callback function
 * @return ZoomBotResult indicating success or failure
 */
ZOOM_BOT_C_API ZoomBotResult zoom_bot_meeting_set_audio_callback(
    MeetingHandle meeting_handle, OnAudioDataReceivedCallback callback);

// HLS video encoder/muxer configuration
typedef struct {
  int width;               // <=0 auto-detected from first frame
  int height;              // <=0 auto-detected from first frame
  int fps;                 // e.g., 10 (default)
  int bitrate_kbps;        // e.g., 3000 (default)
  const char* encoder;     // "auto" (default), "x264", "nvenc"
  const char* preset;      // "veryfast" (default), "medium", "slow", etc.
  const char* hls_prefix;  // "media" (default) - base name for playlist/segments
  // Audio parameters
  int audio_sample_rate;   // e.g., 32000 (default), 44100, 32000, etc.
  int audio_channels;      // e.g., 1 (default stereo), 1 (mono)
  int audio_bitrate_kbps;  // e.g., 128 (default), 96, 192, etc.
} ZoomHlsVideoConfig;

/**
 * Set HLS video callback and start encoding/muxing
 * @param meeting_handle The meeting handle
 * @param callback HLS file callback (NULL clears existing callback and stops
 * encoding)
 * @param config HLS encoder/muxer config; NULL uses defaults
 */
ZOOM_BOT_C_API ZoomBotResult zoom_bot_meeting_set_hls_video_callback(
    MeetingHandle meeting_handle, OnHlsFileCallback callback, const ZoomHlsVideoConfig* config);

/**
 * Run the main event loop to process SDK callbacks
 * This function blocks until interrupted (Ctrl+C) or the loop is stopped
 * Call this after setting up your meetings and callbacks
 */
ZOOM_BOT_C_API void zoom_bot_run_loop();

/**
 * Request the main event loop to stop
 * This function is async-signal-safe and can be called from signal handlers.
 * It is also safe to call from another thread.
 * The loop will stop within ~100ms and zoom_bot_run_loop() will return.
 */
ZOOM_BOT_C_API void zoom_bot_stop_loop();

// Internal helpers in private header `zoom_bot_internal.h`

#ifdef __cplusplus
}
#endif

#endif  // ZOOM_BOT_C_API_H
