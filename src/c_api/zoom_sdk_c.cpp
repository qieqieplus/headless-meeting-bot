#include "zoom_sdk_c.h"
#include "zoom_sdk_audio_delegate.h"
#include "zoom_sdk_video_delegate.h"

#include "ZoomSDK.h"
#include "SDKConfig.h"
#include "Meeting.h"
#include "MeetingConfig.h"
#include "util/Logger.h"
#include "util/Checks.h"

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
#include <functional>
#include <cstdint>

#include "video/FFmpegEncoder.h"
#include "video/HlsMuxer.h"

namespace SDK = ZOOMSDK;

// Global state management
static std::unordered_set<ZoomSDKHandle> g_sdk_instances;
static std::unordered_set<MeetingHandle> g_meeting_instances;
static std::unordered_map<MeetingHandle, OnAudioDataReceivedCallback> g_audio_callbacks;
static GMainLoop* g_main_loop = nullptr;
static std::mutex g_instance_mutex;

// Helpers
template<typename Handle, typename... Maps>
static void erase_from_maps(Handle handle, Maps&... maps) noexcept {
    if (!handle) return;
    std::lock_guard<std::mutex> lock(g_instance_mutex);
    (maps.erase(handle), ...);
}

template<typename T, typename HandleType>
static HandleType create_handle(T* obj, std::unordered_set<HandleType>& instances) noexcept {
    auto handle = reinterpret_cast<HandleType>(obj);
    std::lock_guard<std::mutex> lock(g_instance_mutex);
    instances.insert(handle);
    return handle;
}

template<typename T, typename HandleType>
static T* get_from_handle(HandleType handle, const std::unordered_set<HandleType>& instances) noexcept {
    if (!handle) return nullptr;
    std::lock_guard<std::mutex> lock(g_instance_mutex);
    return instances.count(handle) ? reinterpret_cast<T*>(handle) : nullptr;
}

static ZoomSDKHandle create_sdk_handle(ZoomSDK* sdk) noexcept {
    return create_handle<ZoomSDK, ZoomSDKHandle>(sdk, g_sdk_instances);
}

static MeetingHandle create_meeting_handle(Meeting* meeting) noexcept {
    return create_handle<Meeting, MeetingHandle>(meeting, g_meeting_instances);
}

static ZoomSDK* get_sdk_from_handle(ZoomSDKHandle handle) noexcept {
    return get_from_handle<ZoomSDK, ZoomSDKHandle>(handle, g_sdk_instances);
}

static Meeting* get_meeting_from_handle(MeetingHandle handle) noexcept {
    return get_from_handle<Meeting, MeetingHandle>(handle, g_meeting_instances);
}

static void remove_sdk_handle(ZoomSDKHandle handle) noexcept {
    erase_from_maps(handle, g_sdk_instances);
}

static void remove_meeting_handle(MeetingHandle handle) noexcept {
    erase_from_maps(handle, g_meeting_instances, g_audio_callbacks);
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

// Helper: build HLS encoder/muxer config with defaults and optional overrides
static void buildHlsConfigs(const ZoomHlsVideoConfig* params,
                            FFmpegEncoderConfig& encoderCfg,
                            HlsMuxerConfig& muxerCfg) {
    encoderCfg = FFmpegEncoderConfig{};
    muxerCfg = HlsMuxerConfig{};

    if (params) {
        if (params->width > 0) {
            encoderCfg.width = params->width;
            muxerCfg.width = params->width;
        }
        if (params->height > 0) {
            encoderCfg.height = params->height;
            muxerCfg.height = params->height;
        }
        if (params->fps > 0) {
            encoderCfg.fps = params->fps;
            muxerCfg.fps = params->fps;
        }
        if (params->bitrate_kbps > 0) {
            encoderCfg.bitrateKbps = params->bitrate_kbps;
        }
        if (params->gop_seconds > 0) {
            encoderCfg.gopSeconds = params->gop_seconds;
        }
        if (params->segment_seconds > 0) {
            muxerCfg.segmentSeconds = params->segment_seconds;
        }
        if (params->encoder) {
            encoderCfg.encoder = params->encoder;
        }
        if (params->preset) {
            encoderCfg.preset = params->preset;
        }
        if (params->hls_prefix) {
            muxerCfg.hlsPrefix = params->hls_prefix;
        }
    }
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

    // Create and initialize SDK
    SDKConfig config(std::string(sdk_key), std::string(sdk_secret), "https://zoom.us");

    ZoomSDK* sdk = new ZoomSDK();
    SDK::SDKError result = sdk->initialize(config);

    if (result != SDK::SDKERR_SUCCESS) {
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

    if (result != SDK::SDKERR_SUCCESS ||
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
    Util::Logger::getInstance().success("SDK destroyed successfully");
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
        Util::Logger::getInstance().error("Invalid SDK handle");
        return nullptr;
    }

    if (!sdk->isInitialized() || !sdk->isAuthenticated()) {
        Util::Logger::getInstance().error("SDK not initialized or authenticated");
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
        Util::Logger::getInstance().error("Failed to get required services from SDK");
        return nullptr;
    }

    Meeting* meeting = Meeting::createMeeting(mid, pwd, name, false, token, raw_audio, raw_video, meetingService, settingService);
    if (!meeting) {
        Util::Logger::getInstance().error("Failed to create meeting");
        return nullptr;
    }
    
    MeetingHandle meeting_handle = create_meeting_handle(meeting);
    auto* mediaCtrl = meeting->getMediaController();
    ASSERT_NOT_NULL(mediaCtrl);
    if (raw_audio) {
        auto audioDelegate = new ZoomSDKAudioRawDataDelegate(meeting_handle);
        mediaCtrl->setAudioDelegate(audioDelegate);
    }
    if (raw_video) {
        auto videoDelegate = new ZoomSDKVideoRendererDelegate(meeting_handle);
        mediaCtrl->setVideoDelegate(videoDelegate);
    }

    // Join the meeting
    SDK::SDKError result = meeting->join();
    if (result != SDK::SDKERR_SUCCESS) {
        Util::Logger::getInstance().error("Failed to join meeting, code: " + std::to_string(result));
        delete mediaCtrl->getAudioDelegate();
        delete mediaCtrl->getVideoDelegate();
        delete meeting;
        remove_meeting_handle(meeting_handle);
        return nullptr;
    }

    Util::Logger::getInstance().success("Meeting created and joined successfully");
    return meeting_handle;
}

void zoom_meeting_destroy(MeetingHandle meeting_handle) {
    Meeting* meeting = get_meeting_from_handle(meeting_handle);
    if (!meeting) return;

    meeting->leave();

    // Clear callbacks and delegates
    {
        std::lock_guard<std::mutex> lock(g_instance_mutex);
        g_audio_callbacks.erase(meeting_handle);
    }

    {
        auto* mediaCtrl = meeting->getMediaController();
        ASSERT_NOT_NULL(mediaCtrl);
        delete mediaCtrl->getAudioDelegate();
        delete mediaCtrl->getVideoDelegate();
    }
    
    delete meeting;

    remove_meeting_handle(meeting_handle);
    Util::Logger::getInstance().success("Meeting destroyed successfully");
}

ZoomMeetingStatus zoom_meeting_get_status(MeetingHandle meeting_handle) {
    Meeting* meeting = get_meeting_from_handle(meeting_handle);
    if (!meeting) {
        return ZOOM_MEETING_STATUS_UNKNOWN;
    }

    auto* meetingService = meeting->getMeetingService();
    ASSERT_NOT_NULL(meetingService);

    // Get status from Zoom SDK and cast to our C-compatible enum
    // The enum values match exactly, so this is safe
    SDK::MeetingStatus sdkStatus = meetingService->GetMeetingStatus();
    return static_cast<ZoomMeetingStatus>(sdkStatus);
}

ZoomSDKResult zoom_meeting_set_audio_callback(MeetingHandle meeting_handle, OnAudioDataReceivedCallback callback) {
    Meeting* meeting = get_meeting_from_handle(meeting_handle);
    if (!meeting) return ZOOM_SDK_ERROR;

    std::lock_guard<std::mutex> lock(g_instance_mutex);
    if (callback) {
        g_audio_callbacks[meeting_handle] = callback;
        Util::Logger::getInstance().info("Audio callback set successfully");
    } else {
        g_audio_callbacks.erase(meeting_handle);
        Util::Logger::getInstance().info("Audio callback removed");
    }
    return ZOOM_SDK_SUCCESS;
}

ZoomSDKResult zoom_meeting_set_hls_video_callback(MeetingHandle meeting_handle,
                                                   OnHlsFileCallback callback,
                                                   const ZoomHlsVideoConfig* config) {
    Meeting* meeting = get_meeting_from_handle(meeting_handle);
    if (!meeting) return ZOOM_SDK_ERROR;

    Util::Logger::getInstance().info("Setting HLS video callback");

    FFmpegEncoderConfig encoderCfg;
    HlsMuxerConfig muxerCfg;
    buildHlsConfigs(config, encoderCfg, muxerCfg);

    auto* mediaCtrl = meeting->getMediaController();
    ASSERT_NOT_NULL(mediaCtrl);

    if (!callback) {
        mediaCtrl->clearHlsVideoParams();
        Util::Logger::getInstance().info("HLS video callback removed");
        return ZOOM_SDK_SUCCESS;
    }

    // Capture the user's callback in a lambda routed with the meeting handle
    MediaController::HlsFileCallback cb = [meeting_handle, callback](const char* filename,
                                                                     const uint8_t* data,
                                                                     size_t size,
                                                                     int is_playlist,
                                                                     uint64_t sequence){
        if (callback) {
            callback(meeting_handle, filename, data, size, is_playlist, sequence);
        }
    };

    mediaCtrl->setHlsVideoParams(encoderCfg, muxerCfg, cb);
    // Start immediately only if we're already recording
    // mediaCtrl->startVideoPipeline();
    Util::Logger::getInstance().info("HLS video callback set successfully");
    return ZOOM_SDK_SUCCESS;
}

ZoomSDKResult zoom_meeting_video_encoder_request_idr(MeetingHandle meeting_handle) {
    Meeting* meeting = get_meeting_from_handle(meeting_handle);
    if (!meeting) return ZOOM_SDK_ERROR;
    auto* mediaCtrl = meeting->getMediaController();
    ASSERT_NOT_NULL(mediaCtrl);
    mediaCtrl->requestVideoEncoderIDR();
    return ZOOM_SDK_SUCCESS;
}

void zoom_sdk_run_loop() {
    if (g_main_loop) {
        return;
    }
    Util::Logger::getInstance().info("Starting event loop...");
    g_main_loop = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(g_main_loop);
    g_main_loop_unref(g_main_loop);
    g_main_loop = nullptr;
    Util::Logger::getInstance().info("Event loop stopped");
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

// Push raw video frame into encoding pipeline (used by video delegate)
void zoom_meeting_dispatch_video(MeetingHandle meeting_handle,
                                 const char* y_buffer, const char* u_buffer, const char* v_buffer,
                                 unsigned int width, unsigned int height,
                                 unsigned int buffer_len, unsigned int source_id,
                                 unsigned long long timestamp) {
    if (!y_buffer || !u_buffer || !v_buffer || buffer_len == 0) {
        return;
    }
    Meeting* meeting = get_meeting_from_handle(meeting_handle);
    ASSERT_NOT_NULL(meeting);
    auto* mediaCtrl = meeting->getMediaController();
    ASSERT_NOT_NULL(mediaCtrl);
    mediaCtrl->pushVideoI420(y_buffer, u_buffer, v_buffer, width, height, timestamp);
}

#ifdef __cplusplus
}
#endif
