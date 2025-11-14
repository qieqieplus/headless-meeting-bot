#include "zoom_bot_c.h"

#include <glib.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>

#include "Meeting.h"
#include "SDKConfig.h"
#include "UserController.h"
#include "ZoomSDK.h"
#include "util/Checks.h"
#include "util/Logger.h"
#include "video/EncoderConfig.h"
#include "zoom_bot_audio_delegate.h"
#include "zoom_bot_internal.h"
#include "zoom_bot_share_delegate.h"
#include "zoom_bot_video_delegate.h"

namespace SDK = ZOOMSDK;

namespace {

static GMainLoop* g_main_loop = nullptr;

static bool AuthenticationTimeout(std::mutex& auth_mutex, bool& auth_success, int timeout) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
  GMainContext* ctx = g_main_context_default();
  for (;;) {
    {
      std::lock_guard<std::mutex> lock(auth_mutex);
      if (auth_success) {
        break;
      }
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

static void BuildHlsConfigs(const ZoomHlsVideoConfig* params, VideoEncoderConfig& video_config,
                            AudioEncoderConfig& audio_config, HlsMuxerConfig& muxer_config) {
  video_config = VideoEncoderConfig{};
  audio_config = AudioEncoderConfig{};
  muxer_config = HlsMuxerConfig{};

  if (params) {
    if (params->width > 0) {
      video_config.width = params->width;
    }
    if (params->height > 0) {
      video_config.height = params->height;
    }
    if (params->fps > 0) {
      video_config.fps = params->fps;
    }
    if (params->bitrate_kbps > 0) {
      video_config.bitrate_kbps = params->bitrate_kbps;
    }
    if (params->encoder) {
      video_config.encoder = params->encoder;
    }
    if (params->preset) {
      video_config.preset = params->preset;
    }
    if (params->hls_prefix) {
      muxer_config.hls_prefix = params->hls_prefix;
    }
    if (params->audio_sample_rate > 0) {
      audio_config.sample_rate = params->audio_sample_rate;
    }
    if (params->audio_channels > 0) {
      audio_config.channels = params->audio_channels;
    }
    if (params->audio_bitrate_kbps > 0) {
      audio_config.bitrate_kbps = params->audio_bitrate_kbps;
    }
  }
}

}  // namespace

#ifdef __cplusplus
extern "C" {
#endif

ZoomBotHandle zoom_bot_create(const char* sdk_key, const char* sdk_secret) {
  if (!sdk_key || !sdk_secret) {
    Logger::GetInstance().Error("Invalid SDK key or secret");
    return nullptr;
  }

  SDKConfig config(std::string(sdk_key), std::string(sdk_secret), "https://zoom.us");

  ZoomSDK* sdk = new ZoomSDK();
  auto result = sdk->Initialize(config);

  if (result != SDK::SDKERR_SUCCESS) {
    Logger::GetInstance().Error("Failed to initialize SDK");
    delete sdk;
    return nullptr;
  }

  std::mutex auth_mutex;
  bool auth_success = false;

  result = sdk->Authenticate([&]() {
    std::lock_guard<std::mutex> lock(auth_mutex);
    auth_success = true;
  });

  if (result != SDK::SDKERR_SUCCESS || AuthenticationTimeout(auth_mutex, auth_success, 10)) {
    Logger::GetInstance().Error("Failed to authenticate SDK");
    delete sdk;
    return nullptr;
  }

  Logger::GetInstance().Success("SDK created and authenticated successfully");
  return Impl::CreateSDKHandle(sdk);
}

void zoom_bot_destroy(ZoomBotHandle handle) {
  ZoomSDK* sdk = Impl::GetSDKFromHandle(handle);
  if (!sdk) {
    return;
  }

  Impl::DestroySDKHandle(handle);
  delete sdk;

  zoom_bot_stop_loop();
  Logger::GetInstance().Success("SDK destroyed successfully");
}

MeetingHandle zoom_bot_meeting_create_and_join(ZoomBotHandle sdk_handle, const char* meeting_id,
                                               const char* password, const char* display_name,
                                               const char* join_token, int enable_audio,
                                               int enable_video) {
  ZoomSDK* sdk = Impl::GetSDKFromHandle(sdk_handle);
  if (!sdk) {
    Logger::GetInstance().Error("Invalid SDK handle");
    return nullptr;
  }

  if (!sdk->IsInitialized() || !sdk->IsAuthenticated()) {
    Logger::GetInstance().Error("SDK not initialized or authenticated");
    return nullptr;
  }

  std::string mid = meeting_id ? meeting_id : "";
  std::string pwd = password ? password : "";
  std::string name = display_name ? display_name : "Recording Bot";
  std::string token = join_token ? join_token : "";
  bool raw_audio = (enable_audio != 0);
  bool raw_video = (enable_video != 0);

  auto* meeting_service = sdk->GetMeetingService();
  auto* setting_service = sdk->GetSettingService();

  if (!meeting_service || !setting_service) {
    Logger::GetInstance().Error("Failed to get required services from SDK");
    return nullptr;
  }

  auto meeting = Meeting::CreateMeeting(mid, pwd, name, false, token, raw_audio, raw_video,
                                        meeting_service, setting_service);
  if (!meeting) {
    Logger::GetInstance().Error("Failed to create meeting");
    return nullptr;
  }

  MeetingHandle meeting_handle = Impl::CreateMeetingHandle(meeting.get());
  auto* media_ctrl = meeting->GetMediaController();
  auto* user_ctrl = meeting->GetUserController();
  ASSERT_NOT_NULL(media_ctrl);
  ASSERT_NOT_NULL(user_ctrl);
  if (raw_audio) {
    auto audioDelegate = std::make_unique<ZoomBotAudioRawDataDelegate>(media_ctrl, user_ctrl);
    media_ctrl->SetAudioDelegate(std::move(audioDelegate));
  }
  if (raw_video) {
    media_ctrl->SetVideoDelegateFactory(
        [media_ctrl](auto type_int) -> SDK::IZoomSDKRendererDelegate* {
          auto raw_type = static_cast<ZoomRawDataType>(type_int);
          if (raw_type == ZOOM_RAW_DATA_TYPE_VIDEO) {
            return new ZoomBotVideoRendererDelegate(media_ctrl);
          }
          if (raw_type == ZOOM_RAW_DATA_TYPE_SHARE) {
            return new ZoomBotShareRendererDelegate(media_ctrl);
          }
          return nullptr;
        });
  }

  auto result = meeting->Join();
  if (result != SDK::SDKERR_SUCCESS) {
    Logger::GetInstance().Error("Failed to join meeting, code: " + std::to_string(result));
    Impl::DestroyMeetingHandle(meeting_handle);
    return nullptr;
  }

  // Release ownership to C API caller
  meeting.release();
  Logger::GetInstance().Success("Meeting created successfully");
  return meeting_handle;
}

void zoom_bot_meeting_destroy(MeetingHandle meeting_handle) {
  Meeting* meeting = Impl::GetMeetingFromHandle(meeting_handle);
  if (!meeting) return;

  meeting->Leave();
  Impl::DestroyMeetingHandle(meeting_handle);
  delete meeting;
  Logger::GetInstance().Success("Meeting destroyed successfully");
}

ZoomBotResult zoom_bot_meeting_set_status_callback(MeetingHandle meeting_handle,
                                                   OnMeetingStatusCallback callback) {
  Meeting* meeting = Impl::GetMeetingFromHandle(meeting_handle);
  if (!meeting) return ZOOM_BOT_ERROR;

  if (!callback) {
    meeting->SetOnMeetingStatusChanged(nullptr);
    return ZOOM_BOT_SUCCESS;
  }

  auto on_status = [meeting_handle, callback](ZOOMSDK::MeetingStatus status, int detail) {
    callback(meeting_handle, static_cast<ZoomMeetingStatus>(status), detail);
  };

  meeting->SetOnMeetingStatusChanged(on_status);
  return ZOOM_BOT_SUCCESS;
}

ZoomBotResult zoom_bot_meeting_set_user_status_callback(MeetingHandle meeting_handle,
                                                        OnUserStatusEventCallback callback) {
  Meeting* meeting = Impl::GetMeetingFromHandle(meeting_handle);
  if (!meeting) return ZOOM_BOT_ERROR;

  if (!callback) {
    meeting->GetUserController()->SetOnStatusEvent(nullptr);
    return ZOOM_BOT_SUCCESS;
  }

  auto onEvent = [meeting_handle, callback](const UserStatusEvent& evt) {
    if (!callback) return;

    ZoomUserStatusEvent out{};
    switch (evt.type) {
      case UserStatusEvent::Type::kSnapshot:
        out.event = ZOOM_USER_EVENT_TYPE_SNAPSHOT;
        break;
      case UserStatusEvent::Type::kJoined:
        out.event = ZOOM_USER_EVENT_TYPE_JOINED;
        break;
      case UserStatusEvent::Type::kLeft:
        out.event = ZOOM_USER_EVENT_TYPE_LEFT;
        break;
      case UserStatusEvent::Type::kAudioMuted:
        out.event = ZOOM_USER_EVENT_TYPE_AUDIO_MUTED;
        break;
      case UserStatusEvent::Type::kAudioUnmuted:
        out.event = ZOOM_USER_EVENT_TYPE_AUDIO_UNMUTED;
        break;
      case UserStatusEvent::Type::kVideoOn:
        out.event = ZOOM_USER_EVENT_TYPE_VIDEO_ON;
        break;
      case UserStatusEvent::Type::kVideoOff:
        out.event = ZOOM_USER_EVENT_TYPE_VIDEO_OFF;
        break;
      case UserStatusEvent::Type::kShareStarted:
        out.event = ZOOM_USER_EVENT_TYPE_SHARE_STARTED;
        break;
      case UserStatusEvent::Type::kShareStopped:
        out.event = ZOOM_USER_EVENT_TYPE_SHARE_STOPPED;
        break;
      case UserStatusEvent::Type::kActiveSpeaking:
        out.event = ZOOM_USER_EVENT_TYPE_ACTIVE_SPEAKING;
        break;
      case UserStatusEvent::Type::kInactiveSpeaking:
        out.event = ZOOM_USER_EVENT_TYPE_INACTIVE_SPEAKING;
        break;
    }

    out.user.id = evt.snapshot.id;
    std::string name = evt.snapshot.name;  // copy for safety
    out.user.name = name.c_str();
    out.user.audio = evt.snapshot.audio_on ? 1 : 0;
    out.user.video = evt.snapshot.video_on ? 1 : 0;
    out.user.share = evt.snapshot.sharing ? 1 : 0;
    out.timestamp_ms = evt.timestamp_ms;

    callback(meeting_handle, &out);
  };

  auto* userCtrl = meeting->GetUserController();
  ASSERT_NOT_NULL(userCtrl);
  userCtrl->SetOnStatusEvent(onEvent);

  return ZOOM_BOT_SUCCESS;
}

ZoomBotResult zoom_bot_meeting_set_audio_callback(MeetingHandle meeting_handle,
                                                  OnAudioDataReceivedCallback callback) {
  Meeting* meeting = Impl::GetMeetingFromHandle(meeting_handle);
  if (!meeting) return ZOOM_BOT_ERROR;

  auto& media_config = meeting->GetMediaController()->GetConfig();

  if (callback) {
    media_config.SetAudioCallback(
        [callback, meeting_handle](const uint8_t* pcmData, size_t pcmLength, uint32_t sampleRate,
                                   uint32_t channels, int audioType, uint32_t userId,
                                   uint64_t timestampMs) {
          callback(meeting_handle, reinterpret_cast<const void*>(pcmData),
                   static_cast<int>(pcmLength), audioType, userId);
        });
    Logger::GetInstance().Info("Audio callback set successfully");
  } else {
    media_config.ClearAudioCallback();
    Logger::GetInstance().Info("Audio callback removed");
  }
  return ZOOM_BOT_SUCCESS;
}

ZoomBotResult zoom_bot_meeting_set_hls_video_callback(MeetingHandle meeting_handle,
                                                      OnHlsFileCallback callback,
                                                      const ZoomHlsVideoConfig* config) {
  Meeting* meeting = Impl::GetMeetingFromHandle(meeting_handle);
  if (!meeting) return ZOOM_BOT_ERROR;

  Logger::GetInstance().Info("Setting HLS video callback");

  VideoEncoderConfig video_config;
  AudioEncoderConfig audio_config;
  HlsMuxerConfig muxer_config;
  BuildHlsConfigs(config, video_config, audio_config, muxer_config);

  auto& media_config = meeting->GetMediaController()->GetConfig();

  if (!callback) {
    media_config.ClearHlsMediaParams();
    Logger::GetInstance().Info("HLS video callback removed");
    return ZOOM_BOT_SUCCESS;
  }

  MediaConfig::HlsFileCallback cb = [meeting_handle, callback](const char* filename,
                                                               const uint8_t* data, size_t size,
                                                               int is_playlist, uint64_t sequence) {
    if (callback) {
      callback(meeting_handle, filename, data, size, is_playlist, sequence);
    }
  };

  media_config.SetHlsMediaCallback(video_config, audio_config, muxer_config, cb);
  Logger::GetInstance().Info("HLS video callback set successfully");

  return ZOOM_BOT_SUCCESS;
}

void zoom_bot_run_loop() {
  if (g_main_loop) {
    return;
  }
  Logger::GetInstance().Info("Starting event loop...");
  g_main_loop = g_main_loop_new(nullptr, FALSE);
  g_main_loop_run(g_main_loop);
  g_main_loop_unref(g_main_loop);
  g_main_loop = nullptr;
  Logger::GetInstance().Info("Event loop stopped");
}

static gboolean quit_loop_callback(gpointer _) {
  if (g_main_loop && g_main_loop_is_running(g_main_loop)) {
    g_main_loop_quit(g_main_loop);
  }
  return FALSE;
}

void zoom_bot_stop_loop() {
  if (g_main_loop && g_main_loop_is_running(g_main_loop)) {
    g_main_context_invoke(nullptr, quit_loop_callback, nullptr);
    g_main_context_wakeup(nullptr);
  }
}

#ifdef __cplusplus
}
#endif
