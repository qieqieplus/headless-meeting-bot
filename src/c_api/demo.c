/*
 * Unified Zoom SDK C API Demo
 *
 * Simple demo supporting three modes:
 *   - audio: Record audio only
 *   - video: Record audio + camera video
 *   - share: Record audio + shared screens
 */

#include "zoom_sdk_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Audio callback
void audio_callback(MeetingHandle meeting_handle, const void* data, int length, int type, unsigned int node_id) {
    const char* type_str = "UNKNOWN";
    switch (type) {
        case ZOOM_AUDIO_TYPE_MIXED: type_str = "MIXED"; break;
        case ZOOM_AUDIO_TYPE_ONE_WAY: type_str = "ONE_WAY"; break;
        case ZOOM_AUDIO_TYPE_SHARE: type_str = "SHARE"; break;
    }
    printf("[AUDIO] %d bytes, %s, node: %u\n", length, type_str, node_id);
}

// Video callback (receives both camera video and shared screens)
void video_callback(MeetingHandle meeting_handle, 
                   const char* y_buffer, const char* u_buffer, const char* v_buffer,
                   unsigned int width, unsigned int height, 
                   unsigned int buffer_len, unsigned int source_id,
                   unsigned long long timestamp) {
    printf("[VIDEO] %ux%u, %u bytes, source: %u, ts: %llu\n",
           width, height, buffer_len, source_id, timestamp);
}

void print_usage(const char* program_name) {
    fprintf(stderr, "Usage: %s <mode> <meeting_id> <password>\n\n", program_name);
    fprintf(stderr, "Modes:\n");
    fprintf(stderr, "  audio  - Record audio only\n");
    fprintf(stderr, "  video  - Record audio + shared screens\n\n");
    fprintf(stderr, "Environment Variables:\n");
    fprintf(stderr, "  ZOOM_SDK_KEY     - Required: Your SDK key\n");
    fprintf(stderr, "  ZOOM_SDK_SECRET  - Required: Your SDK secret\n");
    fprintf(stderr, "  ZOOM_JOIN_TOKEN  - Optional: For auto recording auth\n\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  %s audio 1234567890 mypass\n", program_name);
    fprintf(stderr, "  %s video 1234567890 mypass\n", program_name);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        print_usage(argv[0]);
        return 1;
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
        mode_name = "Audio + Shared Screens";
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

    // Create SDK
    printf("Creating SDK...\n");
    ZoomSDKHandle sdk = zoom_sdk_create(sdk_key, sdk_secret);
    if (!sdk) {
        fprintf(stderr, "Failed to create SDK\n");
        return 1;
    }
    printf("SDK ready\n\n");

    // Join meeting
    printf("Joining meeting...\n");
    MeetingHandle meeting = zoom_meeting_create_and_join(
        sdk, meeting_id, password, "Demo Bot", join_token,
        enable_audio, enable_video
    );
    
    if (!meeting) {
        fprintf(stderr, "Failed to join meeting\n");
        zoom_sdk_destroy(sdk);
        return 1;
    }
    printf("Joined successfully\n\n");

    // Set callbacks
    if (enable_audio) {
        zoom_meeting_set_audio_callback(meeting, audio_callback);
        printf("Audio callback registered\n");
    }
    
    if (enable_video) {
        zoom_meeting_set_video_callback(meeting, video_callback);
        printf("Video callback registered (shared screens)\n");
    }

    printf("\nRecording active. Press Ctrl+C to stop.\n");
    printf("-------------------------------------------\n");

    // Run event loop
    zoom_sdk_run_loop();

    // Cleanup
    printf("\n-------------------------------------------\n");
    printf("Cleaning up...\n");
    zoom_meeting_destroy(meeting);
    zoom_sdk_destroy(sdk);
    printf("Done!\n");

    return 0;
}
