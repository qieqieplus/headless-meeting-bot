#include "zoom_bot_c.h"

#include <glib.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>

#include "Meeting.h"
#include "SDKConfig.h"
#include "ZoomSDK.h"
#include "controllers/User.h"
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
    media_ctrl->SetAudioDelegateFactory(
        [media_ctrl, user_ctrl]() -> SDK::IZoomSDKAudioRawDataDelegate* {
          return new ZoomBotAudioRawDataDelegate(media_ctrl, user_ctrl);
        });
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
    out.wall_ts_ms = evt.wall_ts_ms;
    out.media_ts_ms = evt.media_ts_ms;

    callback(meeting_handle, &out);
  };

  auto* userCtrl = meeting->GetUserController();
  ASSERT_NOT_NULL(userCtrl);
  userCtrl->SetOnStatusEvent(onEvent);

  return ZOOM_BOT_SUCCESS;
}

// Helper to build audio encoding config from API config (similar to BuildHlsConfigs for video)
static void BuildAudioEncodingConfig(const ZoomAudioConfig* config, int& sample_rate, int& channels,
                                     std::string& codec, int& bitrate_kbps, bool& use_encoding) {
  // Defaults
  sample_rate = 32000;
  channels = 1;
  codec = "s16le";
  bitrate_kbps = 128;
  use_encoding = false;

  if (!config) {
    return;
  }

  sample_rate = config->sample_rate;
  channels = config->channels;

  // Use enum-based encoding selection
  switch (config->encoding) {
    case ZOOM_AUDIO_ENCODING_AAC:
      codec = "aac";
      use_encoding = true;
      break;
    case ZOOM_AUDIO_ENCODING_MP3:
      codec = "mp3";
      use_encoding = true;
      break;
    case ZOOM_AUDIO_ENCODING_S16LE:
    default:
      codec = "s16le";
      use_encoding = false;
      break;
  }

  if (use_encoding && config->bitrate_kbps > 0) {
    bitrate_kbps = config->bitrate_kbps;
  }
}

ZoomBotResult zoom_bot_meeting_set_audio_callback(MeetingHandle meeting_handle,
                                                  OnAudioDataReceivedCallback callback,
                                                  const ZoomAudioConfig* config) {
  Meeting* meeting = Impl::GetMeetingFromHandle(meeting_handle);
  if (!meeting) return ZOOM_BOT_ERROR;

  Logger::GetInstance().Info("Setting audio callback");

  auto& audio_config_ctrl = meeting->GetMediaController()->GetAudioConfig();

  if (!callback) {
    audio_config_ctrl.ClearAudioCallback();
    Logger::GetInstance().Info("Audio callback removed");
    return ZOOM_BOT_SUCCESS;
  }

  // Build audio config
  int sample_rate, channels, bitrate_kbps;
  std::string codec;
  bool use_encoding;
  BuildAudioEncodingConfig(config, sample_rate, channels, codec, bitrate_kbps, use_encoding);

  // Create unified callback that handles both PCM and encoded audio
  AudioConfig::AudioCallback cb =
      [callback, meeting_handle](const uint8_t* data, size_t size, const char* format,
                                 uint32_t sample_rate, uint32_t channels, int audio_type,
                                 uint32_t user_id, uint64_t timestamp_ms, const char* filename) {
        callback(meeting_handle, reinterpret_cast<const void*>(data), static_cast<int>(size),
                 audio_type, user_id, filename);
      };

  if (use_encoding) {
    // Encoded audio (MP3/AAC)
    if (codec != "aac" && codec != "mp3") {
      Logger::GetInstance().Error("Unsupported audio codec: " + codec);
      return ZOOM_BOT_ERROR;
    }

    AudioConfig::AudioEncodingConfig audio_config;
    audio_config.sample_rate = sample_rate;
    audio_config.channels = channels;
    audio_config.codec = codec;
    audio_config.bitrate_kbps = bitrate_kbps;

    audio_config_ctrl.SetAudioCallback(cb, &audio_config);
    Logger::GetInstance().Info("Audio encoding callback set successfully");
  } else {
    // Raw PCM audio
    audio_config_ctrl.SetAudioCallback(cb, nullptr);
    Logger::GetInstance().Info("Audio callback set successfully");
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

  auto& video_config_ctrl = meeting->GetMediaController()->GetVideoConfig();

  if (!callback) {
    video_config_ctrl.ClearHlsMediaParams();
    Logger::GetInstance().Info("HLS video callback removed");
    return ZOOM_BOT_SUCCESS;
  }

  VideoConfig::HlsFileCallback cb = [meeting_handle, callback](const char* filename,
                                                               const uint8_t* data, size_t size,
                                                               int is_playlist, uint64_t sequence) {
    if (callback) {
      callback(meeting_handle, filename, data, size, is_playlist, sequence);
    }
  };

  video_config_ctrl.SetHlsMediaCallback(video_config, audio_config, muxer_config, cb);
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
