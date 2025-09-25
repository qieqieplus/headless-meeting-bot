/*
 * Example usage of the simplified Zoom SDK C API
 *
 * This demonstrates the "Keep It Simple, Stupid" approach:
 * - Only 5 functions needed for basic functionality
 * - No complex handle management
 * - One-call creation and joining
 * - Simple audio callback setup
 * - Event loop handled automatically by the SDK
 */

#include "zoom_sdk_c.h"
#include <stdio.h>
#include <stdlib.h>

// Simple audio callback that just prints received data info
void audio_callback(MeetingHandle meeting_handle, const void* data, int length, int type, unsigned int node_id) {
    const char* type_str = "UNKNOWN";
    switch (type) {
        case ZOOM_AUDIO_TYPE_MIXED: type_str = "MIXED"; break;
        case ZOOM_AUDIO_TYPE_ONE_WAY: type_str = "ONE_WAY"; break;
        case ZOOM_AUDIO_TYPE_SHARE: type_str = "SHARE"; break;
    }

    printf("[AUDIO] Received %d bytes of %s audio (node_id: %u)\n", length, type_str, node_id);
}

int main(int argc, char *argv[]) {
    if (argc < 3 || (argc - 1) % 2 != 0) {
        fprintf(stderr, "Usage: %s <meeting_id_1> <password_1> [<meeting_id_2> <password_2> ...]\n", argv[0]);
        return 1;
    }

    char* sdk_key = getenv("ZOOM_SDK_KEY");
    char* sdk_secret = getenv("ZOOM_SDK_SECRET");
    if (!sdk_key || !sdk_secret) {
        fprintf(stderr, "Error: Please set ZOOM_SDK_KEY and ZOOM_SDK_SECRET environment variables.\n");
        return 1;
    }

    // Step 1: Create and authenticate SDK in one call
    ZoomSDKHandle sdk = zoom_sdk_create(sdk_key, sdk_secret);
    if (!sdk) {
        printf("Failed to create SDK\n");
        return 1;
    }

    int num_meetings = (argc - 1) / 2;
    MeetingHandle* meetings = (MeetingHandle*)malloc(num_meetings * sizeof(MeetingHandle));
    if (!meetings) {
        printf("Failed to allocate memory for meetings\n");
        zoom_sdk_destroy(sdk);
        return 1;
    }

    for (int i = 0; i < num_meetings; ++i) {
        const char* meeting_id = argv[1 + 2 * i];
        const char* password = argv[2 + 2 * i];
        char bot_name[16];
        snprintf(bot_name, sizeof(bot_name), "Bot %d", i + 1);

        meetings[i] = zoom_meeting_create_and_join(sdk, meeting_id, password, bot_name, 1);
        if (!meetings[i]) {
            printf("Failed to create and join meeting %s\n", meeting_id);
        } else {
            zoom_meeting_set_audio_callback(meetings[i], audio_callback);
            printf("Successfully joined meeting %s as %s\n", meeting_id, bot_name);
        }
    }

    printf("All meetings joined. Press Ctrl+C to leave...\n");

    // Step 4: Run the event loop (blocks until interrupted)
    zoom_sdk_run_loop();

    // Step 5: Clean up (leave meeting and destroy SDK)
    for (int i = 0; i < num_meetings; ++i) {
        if (meetings[i]) {
            zoom_meeting_destroy(meetings[i]);
        }
    }
    
    free(meetings);
    zoom_sdk_destroy(sdk);

    printf("Cleaned up successfully!\n");
    return 0;
}
