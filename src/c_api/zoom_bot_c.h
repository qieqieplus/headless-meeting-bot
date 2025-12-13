#pragma once

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
                                            int length, int type, unsigned int user_id,
                                            const char* filename);

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
  ZOOM_USER_EVENT_TYPE_SHARE_STOPPED = 8,
  ZOOM_USER_EVENT_TYPE_ACTIVE_SPEAKING = 9,
  ZOOM_USER_EVENT_TYPE_INACTIVE_SPEAKING = 10
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
  uint64_t wall_ts_ms;      // Absolute unix epoch timestamp
  int64_t media_ts_ms;      // Media timeline timestamp for event alignment (can be negative)
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

// Audio encoding format
typedef enum {
  ZOOM_AUDIO_ENCODING_S16LE = 0,  // Raw PCM (default)
  ZOOM_AUDIO_ENCODING_AAC = 1,    // AAC-LC encoded
  ZOOM_AUDIO_ENCODING_MP3 = 2     // MP3 encoded
} ZoomAudioEncoding;

// Audio configuration for callback
typedef struct {
  int sample_rate;             // Sample rate in Hz: 8000, 16000, 32000 (default), 44100, 48000
  int channels;                // Number of channels: 1 (mono, default), 2 (stereo)
  ZoomAudioEncoding encoding;  // Encoding format (default: ZOOM_AUDIO_ENCODING_S16LE)
  int bitrate_kbps;  // Bitrate for encoded formats: 64-320 kbps (default: 128); ignored for S16LE
} ZoomAudioConfig;

/**
 * Set audio callback for receiving audio data (raw PCM or encoded)
 * @param meeting_handle The meeting handle
 * @param callback The audio callback function (NULL removes existing callback)
 * @param config Audio configuration; NULL uses defaults (32000 Hz, mono, S16LE)
 * @return ZoomBotResult indicating success or failure
 *
 * Default configuration (when config is NULL):
 * - sample_rate: 32000 Hz
 * - channels: 1 (mono)
 * - encoding: ZOOM_AUDIO_ENCODING_S16LE (raw PCM)
 * - bitrate_kbps: 128 (ignored for S16LE)
 */
ZOOM_BOT_C_API ZoomBotResult zoom_bot_meeting_set_audio_callback(
    MeetingHandle meeting_handle, OnAudioDataReceivedCallback callback,
    const ZoomAudioConfig* config);

// HLS video encoder/muxer configuration
typedef struct {
  int width;               // Video width in pixels; <=0 auto-detected from first frame
  int height;              // Video height in pixels; <=0 auto-detected from first frame
  int fps;                 // Frames per second (default: 10)
  int bitrate_kbps;        // Video bitrate in kbps (default: 3000)
  const char* encoder;     // Encoder: "auto" (default), "x264", "nvenc"
  const char* preset;      // Encoding preset: "veryfast" (default), "medium", "slow"
  const char* hls_prefix;  // Base name for playlist/segments (default: "media")
  // Audio parameters for HLS muxing
  int audio_sample_rate;   // Audio sample rate in Hz (default: 32000)
  int audio_channels;      // Audio channels: 1 (mono, default), 2 (stereo)
  int audio_bitrate_kbps;  // Audio bitrate in kbps (default: 128)
} ZoomHlsVideoConfig;

/**
 * Set HLS video callback and start encoding/muxing
 * @param meeting_handle The meeting handle
 * @param callback HLS file callback (NULL clears existing callback and stops encoding)
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
