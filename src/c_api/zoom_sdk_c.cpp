#include "zoom_sdk_c.h"
#include "zoom_sdk_audio_delegate.h"
#include "zoom_sdk_video_delegate.h"

#include "ZoomSDK.h"
#include "SDKConfig.h"
#include "Meeting.h"
#include "MeetingConfig.h"
#include "util/Logger.h"

#include <iostream>
#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>
#include <glib.h>
#include <memory>
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>

// Global state management
static std::unordered_set<ZoomSDKHandle> g_sdk_instances;
static std::unordered_set<MeetingHandle> g_meeting_instances;
static std::unordered_map<MeetingHandle, OnAudioDataReceivedCallback> g_audio_callbacks;
static std::unordered_map<MeetingHandle, OnVideoDataReceivedCallback> g_video_callbacks;
static GMainLoop* g_main_loop = nullptr;
static std::mutex g_instance_mutex;

// Helper to remove a handle from multiple maps safely
template<typename Handle, typename... Maps>
static void erase_from_maps(Handle handle, Maps&... maps) noexcept {
    if (!handle) return;
    std::lock_guard<std::mutex> lock(g_instance_mutex);
    (maps.erase(handle), ...);
}

// C++ helper functions
static ZoomSDKHandle create_sdk_handle(ZoomSDK* sdk) noexcept {
    auto handle = reinterpret_cast<ZoomSDKHandle>(sdk);
    std::lock_guard<std::mutex> lock(g_instance_mutex);
    g_sdk_instances.insert(handle);
    return handle;
}

static MeetingHandle create_meeting_handle(Meeting* meeting) noexcept {
    auto handle = reinterpret_cast<MeetingHandle>(meeting);
    std::lock_guard<std::mutex> lock(g_instance_mutex);
    g_meeting_instances.insert(handle);
    return handle;
}

static ZoomSDK* get_sdk_from_handle(ZoomSDKHandle handle) noexcept {
    if (!handle) return nullptr;
    std::lock_guard<std::mutex> lock(g_instance_mutex);
    return g_sdk_instances.count(handle) ? reinterpret_cast<ZoomSDK*>(handle) : nullptr;
}

static Meeting* get_meeting_from_handle(MeetingHandle handle) noexcept {
    if (!handle) return nullptr;
    std::lock_guard<std::mutex> lock(g_instance_mutex);
    return g_meeting_instances.count(handle) ? reinterpret_cast<Meeting*>(handle) : nullptr;
}

static void remove_sdk_handle(ZoomSDKHandle handle) noexcept {
    erase_from_maps(handle, g_sdk_instances);
}

static void remove_meeting_handle(MeetingHandle handle) noexcept {
    erase_from_maps(handle, g_meeting_instances, g_audio_callbacks, g_video_callbacks);
}

static bool authentication_timeout(std::mutex& auth_mutex, bool& auth_success, int timeout) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
    GMainContext* ctx = g_main_context_default();
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(auth_mutex);
            if (auth_success) break;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            return true;
        }

        while (g_main_context_pending(ctx)) {
            g_main_context_iteration(ctx, FALSE);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}


#ifdef __cplusplus
extern "C" {
#endif

// === C API IMPLEMENTATION ===

ZoomSDKHandle zoom_sdk_create(const char* sdk_key, const char* sdk_secret) {
    if (!sdk_key || !sdk_secret) {
        Util::Logger::getInstance().error("Invalid SDK key or secret");
        return nullptr;
    }

    // Create SDK configuration
    SDKConfig config(std::string(sdk_key), std::string(sdk_secret), "https://zoom.us");

    // Create and initialize SDK
    ZoomSDK* sdk = new ZoomSDK();
    SDKError result = sdk->initialize(config);

    if (result != SDKERR_SUCCESS) {
        Util::Logger::getInstance().error("Failed to initialize SDK");
        delete sdk;
        return nullptr;
    }

    std::mutex auth_mutex;
    bool auth_success = false;

    result = sdk->authenticate([&]() {
        std::lock_guard<std::mutex> lock(auth_mutex);
        auth_success = true;
    });

    if (result != SDKERR_SUCCESS ||
        authentication_timeout(auth_mutex, auth_success, 10)
    ) {
        Util::Logger::getInstance().error("Failed to authenticate SDK");
        delete sdk;
        return nullptr;
    }

    Util::Logger::getInstance().success("SDK created and authenticated successfully");
    return create_sdk_handle(sdk);
}

void zoom_sdk_destroy(ZoomSDKHandle handle) {
    ZoomSDK* sdk = get_sdk_from_handle(handle);
    if (!sdk) {
        return;
    }
    delete sdk;
    remove_sdk_handle(handle);
    zoom_sdk_stop_loop();
    std::cout << "[ZoomSDK-C] SDK destroyed successfully" << std::endl;
}

MeetingHandle zoom_meeting_create_and_join(ZoomSDKHandle sdk_handle,
                                           const char* meeting_id,
                                           const char* password,
                                           const char* display_name,
                                           const char* join_token,
                                           int enable_audio,
                                           int enable_video) {
    ZoomSDK* sdk = get_sdk_from_handle(sdk_handle);
    if (!sdk) {
        std::cerr << "[ZoomSDK-C] Invalid SDK handle" << std::endl;
        return nullptr;
    }

    if (!sdk->isInitialized() || !sdk->isAuthenticated()) {
        std::cerr << "[ZoomSDK-C] SDK not initialized or authenticated" << std::endl;
        return nullptr;
    }

    std::string mid = meeting_id ? meeting_id : "";
    std::string pwd = password ? password : "";
    std::string name = display_name ? display_name : "Recording Bot";
    std::string token = join_token ? join_token : "";
    bool raw_audio = (enable_audio != 0);
    bool raw_video = (enable_video != 0);

    // Get required services from SDK
    auto* meetingService = sdk->getMeetingService();
    auto* settingService = sdk->getSettingService();

    if (!meetingService || !settingService) {
        std::cerr << "[ZoomSDK-C] Failed to get required services from SDK" << std::endl;
        return nullptr;
    }

    Meeting* meeting = Meeting::createMeeting(mid, pwd, name, false, token, raw_audio, raw_video, meetingService, settingService);
    if (!meeting) {
        std::cerr << "[ZoomSDK-C] Failed to create meeting" << std::endl;
        return nullptr;
    }
    
    MeetingHandle meeting_handle = create_meeting_handle(meeting);
    if (raw_audio) {
        auto audioDelegate = new ZoomSDKAudioRawDataDelegate(meeting_handle);
        meeting->setAudioSource(audioDelegate);
    }
    if (raw_video) {
        auto videoDelegate = new ZoomSDKVideoRendererDelegate(meeting_handle);
        meeting->setVideoSource(videoDelegate);
    }

    // Join the meeting
    SDKError result = meeting->join();
    if (result != SDKERR_SUCCESS) {
        std::cerr << "[ZoomSDK-C] Failed to join meeting, code: " << result << std::endl;
        delete meeting->getAudioSource();
        delete meeting->getVideoSource();
        delete meeting;
        remove_meeting_handle(meeting_handle);
        return nullptr;
    }

    std::cout << "[ZoomSDK-C] Meeting created and joined successfully" << std::endl;
    return meeting_handle;
}

void zoom_meeting_destroy(MeetingHandle meeting_handle) {
    Meeting* meeting = get_meeting_from_handle(meeting_handle);
    if (!meeting) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_instance_mutex);
        g_audio_callbacks.erase(meeting_handle);
        g_video_callbacks.erase(meeting_handle);
    }

    meeting->leave();

    delete meeting->getAudioSource();
    delete meeting->getVideoSource();
    delete meeting;
    remove_meeting_handle(meeting_handle);
    std::cout << "[ZoomSDK-C] Meeting destroyed successfully" << std::endl;
}

ZoomMeetingStatus zoom_meeting_get_status(MeetingHandle meeting_handle) {
    Meeting* meeting = get_meeting_from_handle(meeting_handle);
    if (!meeting) {
        std::cerr << "[ZoomSDK-C] Invalid meeting handle" << std::endl;
        return ZOOM_MEETING_STATUS_UNKNOWN;
    }

    IMeetingService* meetingService = meeting->getMeetingService();
    if (!meetingService) {
        std::cerr << "[ZoomSDK-C] Meeting service not available" << std::endl;
        return ZOOM_MEETING_STATUS_UNKNOWN;
    }

    // Get status from Zoom SDK and cast to our C-compatible enum
    // The enum values match exactly, so this is safe
    ZOOMSDK::MeetingStatus sdkStatus = meetingService->GetMeetingStatus();
    return static_cast<ZoomMeetingStatus>(sdkStatus);
}

ZoomSDKResult zoom_meeting_set_audio_callback(MeetingHandle meeting_handle, OnAudioDataReceivedCallback callback) {
    Meeting* meeting = get_meeting_from_handle(meeting_handle);
    if (!meeting) {
        std::cerr << "[ZoomSDK-C] Invalid meeting handle" << std::endl;
        return ZOOM_SDK_ERROR;
    }

    std::lock_guard<std::mutex> lock(g_instance_mutex);
    if (callback) {
        g_audio_callbacks[meeting_handle] = callback;
        std::cout << "[ZoomSDK-C] Audio callback set successfully" << std::endl;
    } else {
        g_audio_callbacks.erase(meeting_handle);
        std::cout << "[ZoomSDK-C] Audio callback removed" << std::endl;
    }
    return ZOOM_SDK_SUCCESS;
}

ZoomSDKResult zoom_meeting_set_video_callback(MeetingHandle meeting_handle, OnVideoDataReceivedCallback callback) {
    Meeting* meeting = get_meeting_from_handle(meeting_handle);
    if (!meeting) {
        std::cerr << "[ZoomSDK-C] Invalid meeting handle" << std::endl;
        return ZOOM_SDK_ERROR;
    }

    std::lock_guard<std::mutex> lock(g_instance_mutex);
    if (callback) {
        g_video_callbacks[meeting_handle] = callback;
        std::cout << "[ZoomSDK-C] Video callback set successfully" << std::endl;
    } else {
        g_video_callbacks.erase(meeting_handle);
        std::cout << "[ZoomSDK-C] Video callback removed" << std::endl;
    }
    return ZOOM_SDK_SUCCESS;
}

void zoom_sdk_run_loop() {
    if (g_main_loop) {
        return;
    }
    std::cout << "[ZoomSDK-C] Starting event loop..." << std::endl;
    g_main_loop = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(g_main_loop);
    g_main_loop_unref(g_main_loop);
    g_main_loop = nullptr;
    std::cout << "[ZoomSDK-C] Event loop stopped" << std::endl;
}

void zoom_sdk_stop_loop() {
    if (g_main_loop && g_main_loop_is_running(g_main_loop)) {
        g_main_loop_quit(g_main_loop);
    }
}

void zoom_meeting_dispatch_audio(MeetingHandle meeting_handle, const void* data, int length, int type, unsigned int node_id) {
    if (!data || length < 0) {
        return;
    }

    OnAudioDataReceivedCallback callback = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_instance_mutex);
        auto it = g_audio_callbacks.find(meeting_handle);
        if (it != g_audio_callbacks.end()) {
            callback = it->second;
        }
    }

    if (callback) {
        callback(meeting_handle, data, length, type, node_id);
    }
}

void zoom_meeting_dispatch_video(MeetingHandle meeting_handle, 
                                 const char* y_buffer, const char* u_buffer, const char* v_buffer,
                                 unsigned int width, unsigned int height,
                                 unsigned int buffer_len, unsigned int source_id,
                                 unsigned long long timestamp) {
    if (!y_buffer || !u_buffer || !v_buffer || buffer_len == 0) {
        return;
    }

    OnVideoDataReceivedCallback callback = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_instance_mutex);
        auto it = g_video_callbacks.find(meeting_handle);
        if (it != g_video_callbacks.end()) {
            callback = it->second;
        }
    }

    if (callback) {
        callback(meeting_handle, y_buffer, u_buffer, v_buffer, width, height, buffer_len, source_id, timestamp);
    }
}

#ifdef __cplusplus
}
#endif
