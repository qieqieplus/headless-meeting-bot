#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "controllers/Audio.h"
#include "controllers/AudioConfig.h"
#include "controllers/Video.h"
#include "controllers/VideoConfig.h"
#include "events/MeetingRecordingCtrlEvent.h"
#include "meeting_service_components/meeting_audio_interface.h"
#include "meeting_service_components/meeting_recording_interface.h"
#include "meeting_service_components/meeting_sharing_interface.h"
#include "rawdata/rawdata_renderer_interface.h"
#include "util/TimelineClock.h"
#include "zoom_sdk_def.h"

// Forward declarations
namespace ZOOMSDK {
class IZoomSDKRendererDelegate;
class IZoomSDKAudioRawDataDelegate;
}  // namespace ZOOMSDK

// MediaController now acts as a facade/coordinator for VideoController and AudioController
class MediaController {
 public:
  using AudioCallback = AudioConfig::AudioCallback;
  using HlsFileCallback = VideoConfig::HlsFileCallback;
  using VideoDelegateFactory = std::function<ZOOMSDK::IZoomSDKRendererDelegate*(int raw_type)>;
  using AudioDelegateFactory = std::function<ZOOMSDK::IZoomSDKAudioRawDataDelegate*()>;

  explicit MediaController();
  ~MediaController() noexcept;

  // === Configuration ===
  AudioConfig& GetAudioConfig() { return audio_config_; }
  VideoConfig& GetVideoConfig() { return video_config_; }
  const AudioConfig& GetAudioConfig() const { return audio_config_; }
  const VideoConfig& GetVideoConfig() const { return video_config_; }
  void SetVideoDelegateFactory(VideoDelegateFactory factory);
  void SetAudioDelegateFactory(AudioDelegateFactory factory);

  // === Recording Lifecycle ===
  void SetupRecording(ZOOMSDK::IMeetingRecordingController* ctrl, bool use_raw_audio,
                      bool use_raw_video);
  void CleanupRecording();

  // === Video Stream Management ===
  void UpdateShareSources(const std::vector<ZOOMSDK::ZoomSDKSharingSourceInfo>& sources);
  void UpdateCameraStatus(unsigned int user_id, bool video_on);

  // === Audio Stream Management ===
  void UpdateAudioStatus(unsigned int user_id, ZOOMSDK::AudioStatus status);

  // === Data Push (from Zoom SDK callbacks) ===
  void PushVideoI420(StreamKind kind, unsigned int id, const char* y, const char* u, const char* v,
                     unsigned int width, unsigned int height, uint64_t timestamp_ms);
  void PushAudioToHlsPipelines(StreamKind kind, const uint8_t* pcm_data, size_t pcm_length,
                               uint32_t sample_rate, uint32_t channels, uint64_t timestamp_ms);
  void PushAudioEncoded(const uint8_t* pcm_data, size_t pcm_length, uint32_t sample_rate,
                        uint32_t channels, int audio_type, uint32_t user_id, uint64_t timestamp_ms);

  // === Timeline & Status ===
  int64_t Now() const { return timeline_clock_.NowToMediaMs(); }
  int64_t UnixToMediaSignedMs(uint64_t unix_ms) const {
    return timeline_clock_.UnixToMediaMs(unix_ms);
  }
  bool TimelineReady() const { return timeline_clock_.IsBaseSet(); }
  bool IsRecording() const { return is_recording_.load(std::memory_order_acquire); }
  const TimelineClock& GetTimelineClock() const { return timeline_clock_; }

 private:
  ZOOMSDK::SDKError StartMedia(
      bool use_raw_audio, bool use_raw_video,
      const std::vector<ZOOMSDK::ZoomSDKSharingSourceInfo>& initial_shares = {});
  ZOOMSDK::SDKError StopMedia();
  void OnRecordingPrivilegeChanged(bool can_record);
  ZOOMSDK::SDKError StartRawRecording();
  ZOOMSDK::SDKError StopRawRecording();

  AudioConfig audio_config_;
  VideoConfig video_config_;
  TimelineClock timeline_clock_;

  // Delegate controllers
  std::unique_ptr<VideoController> video_controller_;
  std::unique_ptr<AudioController> audio_controller_;

  std::atomic<bool> is_recording_;

  // Recording management
  std::unique_ptr<MeetingRecordingCtrlEvent> recording_event_;
  ZOOMSDK::IMeetingRecordingController* recording_controller_;
  bool use_raw_audio_;
  bool use_raw_video_;
};
