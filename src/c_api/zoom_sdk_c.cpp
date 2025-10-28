#include "zoom_sdk_c.h"
#include "zoom_sdk_audio_delegate.h"
#include "zoom_sdk_video_delegate.h"

#include "Meeting.h"
#include "SDKConfig.h"
#include "ZoomSDK.h"
#include "util/Checks.h"
#include "util/Logger.h"
#include "video/AudioEncoder.h"

#include <chrono>
#include <cstdint>
#include <glib.h>
#include <mutex>
#include <thread>

#include "video/HlsMuxer.h"
#include "video/VideoEncoder.h"
#include "zoom_sdk_internal.h"

namespace SDK = ZOOMSDK;

// Global state management
static GMainLoop *g_main_loop = nullptr;

// Helpers now provided by internal

static bool authentication_timeout(std::mutex &auth_mutex, bool &auth_success,
                                   int timeout) {
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
  GMainContext *ctx = g_main_context_default();
  for (;;) {
    {
      std::lock_guard<std::mutex> lock(auth_mutex);
      if (auth_success)
        break;
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
static void buildHlsConfigs(const ZoomHlsVideoConfig *params,
                            VideoEncoderConfig &encoderCfg,
                            AudioEncoderConfig &audioEncoderCfg,
                            HlsMuxerConfig &muxerCfg) {
  encoderCfg = VideoEncoderConfig{};
  audioEncoderCfg = AudioEncoderConfig{};
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
    // Audio parameters
    if (params->audio_sample_rate > 0) {
      audioEncoderCfg.sampleRate = params->audio_sample_rate;
    }
    if (params->audio_channels > 0) {
      audioEncoderCfg.channels = params->audio_channels;
    }
    if (params->audio_bitrate_kbps > 0) {
      audioEncoderCfg.bitrateKbps = params->audio_bitrate_kbps;
    }
  }
}

#ifdef __cplusplus
extern "C" {
#endif

// === C API IMPLEMENTATION ===

ZoomSDKHandle zoom_sdk_create(const char *sdk_key, const char *sdk_secret) {
  if (!sdk_key || !sdk_secret) {
    Logger::getInstance().error("Invalid SDK key or secret");
    return nullptr;
  }

  // Create and initialize SDK
  SDKConfig config(std::string(sdk_key), std::string(sdk_secret),
                   "https://zoom.us");

  ZoomSDK *sdk = new ZoomSDK();
  SDK::SDKError result = sdk->initialize(config);

  if (result != SDK::SDKERR_SUCCESS) {
    Logger::getInstance().error("Failed to initialize SDK");
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
      authentication_timeout(auth_mutex, auth_success, 10)) {
    Logger::getInstance().error("Failed to authenticate SDK");
    delete sdk;
    return nullptr;
  }

  Logger::getInstance().success("SDK created and authenticated successfully");
  return Impl::createSdkHandle(reinterpret_cast<ZoomSDKHandle>(sdk));
}

void zoom_sdk_destroy(ZoomSDKHandle handle) {
  ZoomSDK *sdk = reinterpret_cast<ZoomSDK *>(Impl::getSdkFromHandle(handle));
  if (!sdk) {
    return;
  }
  delete sdk;
  Impl::destroySdkHandle(handle);
  zoom_sdk_stop_loop();
  Logger::getInstance().success("SDK destroyed successfully");
}

MeetingHandle zoom_meeting_create_and_join(ZoomSDKHandle sdk_handle,
                                           const char *meeting_id,
                                           const char *password,
                                           const char *display_name,
                                           const char *join_token,
                                           int enable_audio, int enable_video) {
  ZoomSDK *sdk =
      reinterpret_cast<ZoomSDK *>(Impl::getSdkFromHandle(sdk_handle));
  if (!sdk) {
    Logger::getInstance().error("Invalid SDK handle");
    return nullptr;
  }

  if (!sdk->isInitialized() || !sdk->isAuthenticated()) {
    Logger::getInstance().error("SDK not initialized or authenticated");
    return nullptr;
  }

  std::string mid = meeting_id ? meeting_id : "";
  std::string pwd = password ? password : "";
  std::string name = display_name ? display_name : "Recording Bot";
  std::string token = join_token ? join_token : "";
  bool raw_audio = (enable_audio != 0);
  bool raw_video = (enable_video != 0);

  // Get required services from SDK
  auto *meetingService = sdk->getMeetingService();
  auto *settingService = sdk->getSettingService();

  if (!meetingService || !settingService) {
    Logger::getInstance().error("Failed to get required services from SDK");
    return nullptr;
  }

  Meeting *meeting =
      Meeting::createMeeting(mid, pwd, name, false, token, raw_audio, raw_video,
                             meetingService, settingService);
  if (!meeting) {
    Logger::getInstance().error("Failed to create meeting");
    return nullptr;
  }

  MeetingHandle meeting_handle =
      Impl::createMeetingHandle(reinterpret_cast<MeetingHandle>(meeting));
  auto *mediaCtrl = meeting->getMediaController();
  ASSERT_NOT_NULL(mediaCtrl);
  if (raw_video) {
    auto videoDelegate = new ZoomSDKVideoRendererDelegate(meeting_handle);
    mediaCtrl->setVideoDelegate(videoDelegate);
  }
  if (raw_audio) {
    auto audioDelegate = new ZoomSDKAudioRawDataDelegate(meeting_handle);
    mediaCtrl->setAudioDelegate(audioDelegate);
  }

  // Join the meeting
  SDK::SDKError result = meeting->join();
  if (result != SDK::SDKERR_SUCCESS) {
    Logger::getInstance().error("Failed to join meeting, code: " +
                                std::to_string(result));
    delete mediaCtrl->getAudioDelegate();
    delete mediaCtrl->getVideoDelegate();
    delete meeting;
    Impl::destroyMeetingHandle(meeting_handle);
    return nullptr;
  }

  Logger::getInstance().success("Meeting created and joined successfully");
  return meeting_handle;
}

void zoom_meeting_destroy(MeetingHandle meeting_handle) {
  Meeting *meeting = Impl::getMeetingFromHandle(meeting_handle);
  if (!meeting)
    return;

  meeting->leave();

  // Clear callbacks and delegates
  {
    auto *mediaCtrl = meeting->getMediaController();
    ASSERT_NOT_NULL(mediaCtrl);
    // Audio/video callbacks are cleared by MediaController destructor
    delete mediaCtrl->getAudioDelegate();
    delete mediaCtrl->getVideoDelegate();
  }

  delete meeting;
  Impl::destroyMeetingHandle(meeting_handle);
  Logger::getInstance().success("Meeting destroyed successfully");
}

ZoomMeetingStatus zoom_meeting_get_status(MeetingHandle meeting_handle) {
  Meeting *meeting = Impl::getMeetingFromHandle(meeting_handle);
  if (!meeting) {
    return ZOOM_MEETING_STATUS_UNKNOWN;
  }

  auto *meetingService = meeting->getMeetingService();
  ASSERT_NOT_NULL(meetingService);

  // Get status from Zoom SDK and cast to our C-compatible enum
  // The enum values match exactly, so this is safe
  SDK::MeetingStatus sdkStatus = meetingService->GetMeetingStatus();
  return static_cast<ZoomMeetingStatus>(sdkStatus);
}

ZoomSDKResult
zoom_meeting_set_audio_callback(MeetingHandle meeting_handle,
                                OnAudioDataReceivedCallback callback) {
  Meeting *meeting = Impl::getMeetingFromHandle(meeting_handle);
  if (!meeting)
    return ZOOM_SDK_ERROR;

  auto *mediaCtrl = meeting->getMediaController();
  ASSERT_NOT_NULL(mediaCtrl);

  if (callback) {
    // Wrap the C callback in a lambda that matches MediaController's
    // AudioCallback signature
    mediaCtrl->setAudioCallback(
        [callback, meeting_handle](const uint8_t *pcmData, size_t pcmLength,
                                   uint32_t sampleRate, uint32_t channels,
                                   int audioType, uint32_t userId,
                                   uint64_t timestampMs) {
          // Convert back to C API format
          callback(meeting_handle, reinterpret_cast<const void *>(pcmData),
                   static_cast<int>(pcmLength), audioType, userId);
        });
    Logger::getInstance().info("Audio callback set successfully");
  } else {
    mediaCtrl->setAudioCallback(nullptr);
    Logger::getInstance().info("Audio callback removed");
  }
  return ZOOM_SDK_SUCCESS;
}

ZoomSDKResult
zoom_meeting_set_hls_video_callback(MeetingHandle meeting_handle,
                                    OnHlsFileCallback callback,
                                    const ZoomHlsVideoConfig *config) {
  Meeting *meeting = Impl::getMeetingFromHandle(meeting_handle);
  if (!meeting)
    return ZOOM_SDK_ERROR;

  Logger::getInstance().info("Setting HLS video callback");

  VideoEncoderConfig encoderCfg;
  AudioEncoderConfig audioEncoderCfg;
  HlsMuxerConfig muxerCfg;
  buildHlsConfigs(config, encoderCfg, audioEncoderCfg, muxerCfg);

  auto *mediaCtrl = meeting->getMediaController();
  ASSERT_NOT_NULL(mediaCtrl);

  if (!callback) {
    mediaCtrl->clearHlsMediaParams();
    Logger::getInstance().info("HLS video callback removed");
    return ZOOM_SDK_SUCCESS;
  }

  // Capture the user's callback in a lambda routed with the meeting handle
  MediaController::HlsFileCallback cb =
      [meeting_handle, callback](const char *filename, const uint8_t *data,
                                 size_t size, int is_playlist,
                                 uint64_t sequence) {
        if (callback) {
          callback(meeting_handle, filename, data, size, is_playlist, sequence);
        }
      };

  mediaCtrl->setHlsMediaParams(encoderCfg, audioEncoderCfg, muxerCfg, cb);
  // Start immediately only if we're already recording
  // mediaCtrl->startVideoPipeline();
  Logger::getInstance().info("HLS video callback set successfully");
  return ZOOM_SDK_SUCCESS;
}

ZoomSDKResult
zoom_meeting_video_encoder_request_idr(MeetingHandle meeting_handle) {
  Meeting *meeting = Impl::getMeetingFromHandle(meeting_handle);
  if (!meeting)
    return ZOOM_SDK_ERROR;
  auto *mediaCtrl = meeting->getMediaController();
  ASSERT_NOT_NULL(mediaCtrl);
  mediaCtrl->requestVideoEncoderIDR();
  return ZOOM_SDK_SUCCESS;
}

void zoom_sdk_run_loop() {
  if (g_main_loop) {
    return;
  }
  Logger::getInstance().info("Starting event loop...");
  g_main_loop = g_main_loop_new(nullptr, FALSE);
  g_main_loop_run(g_main_loop);
  g_main_loop_unref(g_main_loop);
  g_main_loop = nullptr;
  Logger::getInstance().info("Event loop stopped");
}

void zoom_sdk_stop_loop() {
  if (g_main_loop && g_main_loop_is_running(g_main_loop)) {
    g_main_loop_quit(g_main_loop);
  }
}

#ifdef __cplusplus
}
#endif
