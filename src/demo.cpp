#include <csignal>
#include <vector>
#include <glib.h>
#include "SDKConfig.h"
#include "MeetingConfig.h"
#include "ZoomSDK.h"
#include "Meeting.h"
#include "util/Logger.h"

using namespace std;
using namespace ZOOMSDK;

// Global meeting instances for signal handling
vector<Meeting*> g_meetings;
GMainLoop* g_eventLoop = nullptr;
ZoomSDK* g_sdk = nullptr;

/**
 *  Callback fired atexit()
 */
void onExit() {
    // Clean up all meetings
    for (auto* meeting : g_meetings) {
        if (meeting) {
            meeting->leave();
            delete meeting;
        }
    }
    g_meetings.clear();
    
    // Cleanup SDK
    if (g_sdk) {
        g_sdk->cleanup();
        delete g_sdk;
        g_sdk = nullptr;
    }

    Util::Logger::getInstance().info("exiting...");
}

/**
 * Callback fired when a signal is trapped
 * @param signal type of signal
 */
void onSignal(int signal) {
    onExit();
    _Exit(signal);
}


/**
 * Callback for glib event loop
 * @param data event data
 * @return always TRUE
 */
gboolean onTimeout (gpointer data) {
    return TRUE;
}

/**
 * Demo function showing different ways to create meetings on the fly
 */
void createMultipleMeetings() {
    Util::Logger::getInstance().info("=== Demonstrating Multiple Meeting Creation ===");

    // Get required services from SDK
    auto* meetingService = g_sdk->getMeetingService();
    auto* settingService = g_sdk->getSettingService();

    if (!meetingService || !settingService) {
        Util::Logger::getInstance().error("Failed to get required services from SDK");
        return;
    }

    // Method 1: Using Meeting factory methods
    Util::Logger::getInstance().info("Creating meeting using Meeting factory method...");
    auto* meeting1 = Meeting::createMeeting("71164894209", "JRt3UL", "Bot 1", false, "", true, false, meetingService, settingService);
    if (meeting1) {
        g_meetings.push_back(meeting1);
        meeting1->join();
    }

    Util::Logger::getInstance().success("Created " + to_string(g_meetings.size()) + " meetings successfully!");
}

/**
 * Run the Zoom Meeting Bot
 * @param argc argument count
 * @param argv argument vector
 * @return SDKError
 */
SDKError run(int argc, char** argv) {
    SDKError err{SDKERR_SUCCESS};
    
    // Create SDK configuration
    SDKConfig sdkConfig("TJXdawDvQa26OmhEtQkv6A", 
                       "49vGbi691Z1yDB2U2Fi6HbUjjdpt3Ngn", 
                       "https://zoom.us");
    
    // Initialize ZoomSDK with SDK configuration
    g_sdk = new ZoomSDK();
    
    err = g_sdk->initialize(sdkConfig);
    if (ZoomSDK::hasError(err, "initialize SDK"))
        return err;

    // Authenticate SDK with callback to create multiple meetings
    err = g_sdk->authenticate([]() {
        createMultipleMeetings();
    });
    

    if (ZoomSDK::hasError(err, "authenticate SDK"))
        return err;

    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    atexit(onExit);

    return err;
}

int main(int argc, char **argv) {
    SDKError err = run(argc, argv);
    if (err != SDKERR_SUCCESS)
        return err;

    // Use an event loop to receive callbacks
    g_eventLoop = g_main_loop_new(NULL, FALSE);
    g_timeout_add(100, onTimeout, g_eventLoop);
    g_main_loop_run(g_eventLoop);

    return err;
}
