#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

#include "controllers/BaseStream.h"
#include "controllers/VideoConfig.h"
#include "meeting_service_components/meeting_sharing_interface.h"
#include "rawdata/rawdata_renderer_interface.h"
#include "rawdata/zoom_rawdata_api.h"
#include "util/TimelineClock.h"
#include "zoom_sdk_def.h"

// Forward declarations
class MediaEncodePipeline;
struct AudioEncoderConfig;
struct HlsMuxerConfig;
struct VideoEncoderConfig;

class VideoController : public BaseStreamController<StreamKey, struct VideoStreamState,
                                                    StreamKeyHash, VideoController> {
 public:
  using HlsFileCallback = VideoConfig::HlsFileCallback;
  using VideoDelegateFactory = std::function<ZOOMSDK::IZoomSDKRendererDelegate*(int raw_type)>;

  explicit VideoController(VideoConfig& config, const TimelineClock& timeline_clock);
  ~VideoController() noexcept;

  // === Configuration ===
  void SetVideoDelegateFactory(VideoDelegateFactory factory) {
    video_delegate_factory_ = std::move(factory);
  }

  // === Stream Management ===
  void UpdateShareSources(const std::vector<ZOOMSDK::ZoomSDKSharingSourceInfo>& sources);
  void UpdateCameraStatus(unsigned int user_id, bool video_on);

  // === Data Push ===
  void PushVideoFrame(StreamKind kind, unsigned int id, const char* y, const char* u, const char* v,
                      unsigned int width, unsigned int height, uint64_t timestamp_ms);
  // Push audio PCM to video HLS pipelines for muxing
  void PushAudioForHls(StreamKind kind, const uint8_t* pcm_data, size_t pcm_length,
                       uint32_t sample_rate, uint32_t channels, uint64_t timestamp_ms);

  // === Recording Lifecycle ===
  void StartRecording();
  void StopRecording();

 private:
  struct ZoomRendererDeleter {
    void operator()(ZOOMSDK::IZoomSDKRenderer* renderer) const {
      if (renderer) {
        ZOOMSDK::destroyRenderer(renderer);
      }
    }
  };

  friend struct VideoStreamState;

  // Helper functions
  void LogStreamEvent(const StreamKey& key, const std::string& event,
                      const std::string& level = "success");
  bool ActivateStreamComponents(VideoStreamState& state, const StreamKey& key);
  bool CreateVideoRenderer(VideoStreamState& state, const StreamKey& key);
  bool CreateVideoPipeline(VideoStreamState& state, const StreamKey& key);
  void SubscribeVideoRenderer(VideoStreamState& state, const StreamKey& key);
  StreamKey GetHlsStreamKey(const StreamKey& key) const;

  void EnsureStream(const StreamKey& key);
  void DestroyStream(const StreamKey& key);

  VideoConfig& config_;
  const TimelineClock& timeline_clock_;
  VideoDelegateFactory video_delegate_factory_;
  std::atomic<bool> is_recording_;

  // Active share sources for mapping shareSourceID to user_id in HLS filenames
  mutable std::mutex active_share_sources_mtx_;
  std::vector<ZOOMSDK::ZoomSDKSharingSourceInfo> active_share_sources_;
};

struct VideoStreamState {
  std::shared_ptr<MediaEncodePipeline> pipeline;
  std::unique_ptr<ZOOMSDK::IZoomSDKRenderer, VideoController::ZoomRendererDeleter> renderer;
  // Note: delegate is owned by the SDK (passed to createRenderer), not stored here
};
