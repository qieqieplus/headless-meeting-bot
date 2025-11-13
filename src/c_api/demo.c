/*
 * Unified Zoom SDK C API Demo
 *
 * Simple demo supporting two modes:
 *   - audio: Record audio only
 *   - video: Record audio + shared screens as HLS (fMP4 segments)
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "zoom_bot_c.h"

// Global flag for graceful shutdown
volatile sig_atomic_t should_exit = 0;

// Signal handler for graceful shutdown
// IMPORTANT: Only async-signal-safe operations are allowed here
void signal_handler(int signum) {
  // Set flags and request loop stop - all async-signal-safe
  should_exit = 1;
  zoom_bot_stop_loop();
  // Note: printf() is NOT async-signal-safe and can deadlock
  // We'll print the shutdown message in main() after the loop exits
}

// Audio callback
void audio_callback(MeetingHandle meeting_handle, const void* data, int length, int type,
                    unsigned int node_id) {
  const char* type_str = "UNKNOWN";
  switch (type) {
    case ZOOM_AUDIO_TYPE_MIXED:
      type_str = "MIXED";
      break;
    case ZOOM_AUDIO_TYPE_ONE_WAY:
      type_str = "ONE_WAY";
      break;
    case ZOOM_AUDIO_TYPE_SHARE:
      type_str = "SHARE";
      break;
  }
  // printf("[AUDIO] %d bytes, %s, node: %u\n", length, type_str, node_id);
}

// HLS file callback - saves each file (init.mp4, segments, playlist) to disk
void hls_file_callback(MeetingHandle meeting_handle, const char* filename,
                       const unsigned char* data, size_t size, int is_playlist, uint64_t sequence) {
  FILE* f = fopen(filename, "wb");
  if (!f) {
    fprintf(stderr, "[HLS] Failed to open %s for writing\n", filename);
    return;
  }

  size_t written = fwrite(data, 1, size, f);
  fclose(f);

  if (written != size) {
    fprintf(stderr, "[HLS] Write error for %s: wrote %zu/%zu bytes\n", filename, written, size);
    return;
  }

  const char* type = is_playlist ? "PLAYLIST" : "SEGMENT";
  printf("[HLS] Wrote %s: %s (%zu bytes, seq=%llu)\n", type, filename, size,
         (unsigned long long)sequence);
}

const char* user_event_type_to_string(ZoomUserEventType type) {
  switch (type) {
    case ZOOM_USER_EVENT_TYPE_SNAPSHOT:
      return "SNAPSHOT";
    case ZOOM_USER_EVENT_TYPE_JOINED:
      return "JOINED";
    case ZOOM_USER_EVENT_TYPE_LEFT:
      return "LEFT";
    case ZOOM_USER_EVENT_TYPE_AUDIO_MUTED:
      return "AUDIO_MUTED";
    case ZOOM_USER_EVENT_TYPE_AUDIO_UNMUTED:
      return "AUDIO_UNMUTED";
    case ZOOM_USER_EVENT_TYPE_VIDEO_ON:
      return "VIDEO_ON";
    case ZOOM_USER_EVENT_TYPE_VIDEO_OFF:
      return "VIDEO_OFF";
    case ZOOM_USER_EVENT_TYPE_SHARE_STARTED:
      return "SHARE_STARTED";
    case ZOOM_USER_EVENT_TYPE_SHARE_STOPPED:
      return "SHARE_STOPPED";
    default:
      return "UNKNOWN";
  }
}

void user_status_callback(MeetingHandle handle, const ZoomUserStatusEvent* evt) {
  printf("[USER] %-15s | %s (%u) | audio:%d video:%d share:%d\n",
         user_event_type_to_string(evt->event), evt->user.name, evt->user.id, evt->user.audio,
         evt->user.video, evt->user.share);
}

void print_usage(const char* program_name) {
  fprintf(stderr, "Usage: %s <mode> <meeting_id> <password>\n\n", program_name);
  fprintf(stderr, "Modes:\n");
  fprintf(stderr, "  audio  - Record audio only\n");
  fprintf(stderr, "  video  - Record audio + shared screens (HLS fMP4)\n\n");
  fprintf(stderr, "Environment Variables:\n");
  fprintf(stderr, "  ZOOM_SDK_KEY     - Required: Your SDK key\n");
  fprintf(stderr, "  ZOOM_SDK_SECRET  - Required: Your SDK secret\n");
  fprintf(stderr, "  ZOOM_JOIN_TOKEN  - Optional: For auto recording auth\n\n");
  fprintf(stderr, "Examples:\n");
  fprintf(stderr, "  %s audio 1234567890 mypass\n", program_name);
  fprintf(stderr, "  %s video 1234567890 mypass\n", program_name);
  fprintf(stderr, "\nOutput:\n");
  fprintf(stderr, "  Video mode creates: media.m3u8, init.mp4, media-seg-*.m4s\n");
  fprintf(stderr, "  Play with: vlc media.m3u8 or ffplay media.m3u8\n");
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    print_usage(argv[0]);
    return 0;
  }

  const char* mode = argv[1];
  const char* meeting_id = argv[2];
  const char* password = argv[3];

  // Parse mode
  int enable_audio = 0;
  int enable_video = 0;
  const char* mode_name = NULL;

  if (strcmp(mode, "audio") == 0) {
    enable_audio = 1;
    mode_name = "Audio Only";
  } else if (strcmp(mode, "video") == 0) {
    enable_audio = 1;
    enable_video = 1;
    mode_name = "Audio + HLS Video (Shared Screens)";
  } else {
    fprintf(stderr, "Error: Invalid mode '%s'\n\n", mode);
    print_usage(argv[0]);
    return 1;
  }

  // Get credentials
  char* sdk_key = getenv("ZOOM_SDK_KEY");
  char* sdk_secret = getenv("ZOOM_SDK_SECRET");
  char* join_token = getenv("ZOOM_JOIN_TOKEN");

  if (!sdk_key || !sdk_secret) {
    fprintf(stderr, "Error: ZOOM_SDK_KEY and ZOOM_SDK_SECRET must be set\n");
    return 1;
  }

  // Print configuration
  printf("=== Zoom SDK Demo ===\n");
  printf("Mode:       %s\n", mode_name);
  printf("Meeting:    %s\n", meeting_id);
  printf("Join Token: %s\n\n", join_token ? join_token : "(none)");

  // Set up signal handler for graceful shutdown
  signal(SIGINT, signal_handler);

  // Create SDK
  printf("Creating SDK...\n");
  ZoomBotHandle sdk = zoom_bot_create(sdk_key, sdk_secret);
  if (!sdk) {
    fprintf(stderr, "Failed to create SDK\n");
    return 1;
  }
  printf("SDK ready\n\n");

  // Join meeting
  MeetingHandle meeting = zoom_bot_meeting_create_and_join(sdk, meeting_id, password, "Demo Bot",
                                                           join_token, enable_audio, enable_video);

  if (!meeting) {
    fprintf(stderr, "Failed to join meeting\n");
    zoom_bot_destroy(sdk);
    return 1;
  }

  // Set callbacks
  zoom_bot_meeting_set_user_status_callback(meeting, user_status_callback);
  printf("User status callback registered\n");

  if (enable_audio) {
    zoom_bot_meeting_set_audio_callback(meeting, audio_callback);
    printf("Audio callback registered\n");
  }

  if (enable_video) {
    // Configure HLS encoder/muxer
    ZoomHlsVideoConfig config = {
        .hls_prefix = "",  // Output: share.m3u8, share-part-*.m4s
    };
    zoom_bot_meeting_set_hls_video_callback(meeting, hls_file_callback, &config);
    printf("HLS video callback registered (shared screens -> fMP4)\n");
  }

  printf("\nRecording active. Press Ctrl+C to stop gracefully.\n");
  printf("-------------------------------------------\n");

  // Run event loop
  zoom_bot_run_loop();

  // Cleanup
  printf("\n-------------------------------------------\n");
  if (should_exit) {
    printf("Graceful shutdown initiated. Cleaning up resources...\n");
  } else {
    printf("Recording stopped. Cleaning up...\n");
  }
  zoom_bot_meeting_destroy(meeting);
  zoom_bot_destroy(sdk);
  printf("Cleanup complete!\n");

  if (enable_video) {
    printf("\nTo play the recording:\n");
    printf("  vlc media.m3u8\n");
    printf("  ffplay media.m3u8\n");
  }

  // Force immediate exit to avoid hanging on Zoom SDK internal threads
  // The SDK doesn't always properly clean up all background threads on Linux
  // Use _exit() instead of exit() to bypass atexit handlers that might block
  _exit(0);
}
