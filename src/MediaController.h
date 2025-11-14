#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "MediaConfig.h"
#include "events/MeetingRecordingCtrlEvent.h"
#include "meeting_service_components/meeting_recording_interface.h"
#include "meeting_service_components/meeting_sharing_interface.h"
#include "rawdata/rawdata_audio_helper_interface.h"
#include "rawdata/rawdata_renderer_interface.h"
#include "rawdata/zoom_rawdata_api.h"
#include "util/TimelineClock.h"
#include "zoom_sdk_def.h"

// Forward declarations
class MediaEncodePipeline;
struct AudioEncoderConfig;
struct HlsMuxerConfig;
struct VideoEncoderConfig;
namespace ZOOMSDK {
class IZoomSDKRendererDelegate;
}

class MediaController {
 public:
  using HlsFileCallback = MediaConfig::HlsFileCallback;
  using AudioCallback = MediaConfig::AudioCallback;
  using VideoDelegateFactory = std::function<ZOOMSDK::IZoomSDKRendererDelegate*(int raw_type)>;

  explicit MediaController(const std::string& meeting_id = "");
  ~MediaController() noexcept;

  void SetVideoDelegateFactory(VideoDelegateFactory factory) {
    video_delegate_factory_ = std::move(factory);
  }
  void SetAudioDelegate(std::unique_ptr<ZOOMSDK::IZoomSDKAudioRawDataDelegate> delegate);

  MediaConfig& GetConfig() { return config_; }
  const MediaConfig& GetConfig() const { return config_; }

  void SetupRecording(ZOOMSDK::IMeetingRecordingController* ctrl, bool use_raw_audio,
                      bool use_raw_video);
  void CleanupRecording();

  void UpdateShareSources(const std::vector<ZOOMSDK::ZoomSDKSharingSourceInfo>& sources);
  void UpdateCameraStatus(unsigned int user_id, bool video_on);
  void EnsureStream(const StreamKey& key);
  void DestroyStream(const StreamKey& key);
  void IteratePipeline(const std::function<void(const StreamKey&, MediaEncodePipeline&)>& fn);

  void PushVideoI420ForSource(StreamKind kind, unsigned int id, const char* y, const char* u,
                              const char* v, unsigned int width, unsigned int height,
                              uint64_t timestamp_ms);

  // audio track for stream
  void PushAudioPCM(StreamKind kind, const uint8_t* pcm_data, size_t pcm_length,
                    uint32_t sample_rate, uint32_t channels, uint64_t timestamp_ms);

  // realtime callback
  void DispatchAudio(const uint8_t* pcm_data, size_t pcm_length, uint32_t sample_rate,
                     uint32_t channels, int audio_type, uint32_t user_id, uint64_t timestamp_ms);
  void RequestVideoEncoderIdr();

  uint64_t Now() const { return timeline_clock_.NowToMediaMs(); }
  bool TimelineReady() const { return timeline_clock_.IsBaseSet(); }
  bool IsRecording() const { return is_recording_.load(std::memory_order_acquire); }

 private:
  struct StreamState {
    std::shared_ptr<MediaEncodePipeline> pipeline;
    std::unique_ptr<ZOOMSDK::IZoomSDKRenderer> renderer;
    std::unique_ptr<ZOOMSDK::IZoomSDKRendererDelegate> delegate;
  };

  void CreateRendererForStream(StreamState& state, const StreamKey& key);
  void CreatePipelineForStream(StreamState& state, const StreamKey& key);
  void SubscribeRendererForStream(StreamState& state, const StreamKey& key);

  ZOOMSDK::SDKError StartMedia(
      bool use_raw_audio, bool use_raw_video,
      const std::vector<ZOOMSDK::ZoomSDKSharingSourceInfo>& initial_shares = {});
  ZOOMSDK::SDKError StopMedia();
  void OnRecordingPrivilegeChanged(bool can_record);
  ZOOMSDK::SDKError StartRawRecording();
  ZOOMSDK::SDKError StopRawRecording();
  template <typename F>
  void IterateStream(F fn) {
    std::lock_guard<std::mutex> lock(media_mtx_);
    for (auto& [key, state] : streams_) {
      fn(key, state);
    }
  }

  MediaConfig config_;
  VideoDelegateFactory video_delegate_factory_;
  ZOOMSDK::IZoomSDKAudioRawDataHelper* audio_helper_;
  std::unique_ptr<ZOOMSDK::IZoomSDKAudioRawDataDelegate> audio_delegate_;
  std::atomic<bool> is_recording_;
  TimelineClock timeline_clock_;
  mutable std::mutex media_mtx_;
  std::unordered_map<StreamKey, StreamState> streams_;

  // Recording management
  std::unique_ptr<MeetingRecordingCtrlEvent> recording_event_;
  ZOOMSDK::IMeetingRecordingController* recording_controller_;
  bool use_raw_audio_;
  bool use_raw_video_;
};
