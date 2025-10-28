#ifndef HEADLESS_ZOOM_BOT_MEDIA_CONTROLLER_H
#define HEADLESS_ZOOM_BOT_MEDIA_CONTROLLER_H

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "meeting_service_components/meeting_audio_interface.h"
#include "meeting_service_components/meeting_sharing_interface.h"
#include "rawdata/rawdata_audio_helper_interface.h"
#include "rawdata/rawdata_renderer_interface.h"
#include "rawdata/zoom_rawdata_api.h"
#include "zoom_sdk_def.h"

// Forward declarations
struct VideoEncoderConfig;
struct AudioEncoderConfig;
struct HlsMuxerConfig;
class MediaEncodePipeline;

/**
 * MediaController encapsulates all media handling (audio/video) for a Meeting.
 * Manages video encoding pipeline lifecycle, HLS configuration, and frame
 * dispatch.
 */
class MediaController {
public:
  using HlsFileCallback =
      std::function<void(const char *filename, const uint8_t *data, size_t size,
                         int is_playlist, uint64_t sequence)>;

  using AudioCallback = std::function<void(
      const uint8_t *pcmData, size_t pcmLength, uint32_t sampleRate,
      uint32_t channels, int audioType, uint32_t userId, uint64_t timestampMs)>;

  MediaController();
  ~MediaController();

  // Set audio/video delegates (must be called before starting recording)
  void setAudioDelegate(ZOOMSDK::IZoomSDKAudioRawDataDelegate *delegate);
  void setVideoDelegate(ZOOMSDK::IZoomSDKRendererDelegate *delegate);

  ZOOMSDK::IZoomSDKAudioRawDataDelegate *getAudioDelegate() const {
    return m_audioDelegate;
  }
  ZOOMSDK::IZoomSDKRendererDelegate *getVideoDelegate() const {
    return m_videoDelegate;
  }

  // Set callbacks
  void setAudioCallback(AudioCallback cb) { m_audioCallback = std::move(cb); }
  void setHlsMediaParams(const VideoEncoderConfig &videoEncCfg,
                         const AudioEncoderConfig &audioEncCfg,
                         const HlsMuxerConfig &muxCfg, HlsFileCallback cb);

  // HLS media configuration
  void clearHlsMediaParams();

  // Start/stop raw recording (handles audio/video subscription)
  ZOOMSDK::SDKError startMedia(
      bool useRawAudio, bool useRawVideo,
      const std::vector<ZOOMSDK::ZoomSDKSharingSourceInfo> &currentShares = {});
  ZOOMSDK::SDKError stopMedia();

  // Share event handlers (called by Meeting's share event)
  void onShareStart(const ZOOMSDK::ZoomSDKSharingSourceInfo &shareInfo);
  void onShareEnd(const ZOOMSDK::ZoomSDKSharingSourceInfo &shareInfo);

  // Pipeline lifecycle (tied to recording state)
  void startVideoPipeline();
  void stopVideoPipeline();

  // Video frame dispatch
  void pushVideoI420(const char *y, const char *u, const char *v,
                     unsigned int width, unsigned int height,
                     unsigned long long timestampMs);

  // Audio frame dispatch
  void pushAudioPCM(const uint8_t *pcmData, size_t pcmLength,
                    uint32_t sampleRate, uint32_t channels,
                    uint64_t timestampMs);

  // Raw audio callback dispatch
  void dispatchAudio(const uint8_t *pcmData, size_t pcmLength,
                     uint32_t sampleRate, uint32_t channels, int audioType,
                     uint32_t userId, uint64_t timestampMs);

  // Encoder control
  void requestVideoEncoderIDR();

  // Thread-safe pipeline access
  std::shared_ptr<MediaEncodePipeline> getMediaPipelineShared() const;

  // Query state
  bool isRecording() const { return m_isRecording; }

private:
  ZOOMSDK::SDKError
  subscribeShare(const ZOOMSDK::ZoomSDKSharingSourceInfo &shareInfo);
  ZOOMSDK::SDKError
  unSubscribeShare(const ZOOMSDK::ZoomSDKSharingSourceInfo &shareInfo);

  // Audio/video helpers and delegates
  ZOOMSDK::IZoomSDKAudioRawDataHelper *m_audioHelper;
  ZOOMSDK::IZoomSDKAudioRawDataDelegate *m_audioDelegate;
  ZOOMSDK::IZoomSDKRenderer *m_videoHelper;
  ZOOMSDK::IZoomSDKRendererDelegate *m_videoDelegate;

  // Synchronizes access to share state and renderer subscription
  mutable std::mutex m_shareMtx;

  // Share tracking (most-recent share on top)
  std::vector<unsigned int> m_shareSourceIds = {};

  // Recording state
  bool m_isRecording;

  // Media pipeline state
  std::shared_ptr<MediaEncodePipeline> m_mediaPipeline;
  std::unique_ptr<VideoEncoderConfig> m_videoEncoderCfg;
  std::unique_ptr<AudioEncoderConfig> m_audioEncoderCfg;
  std::unique_ptr<HlsMuxerConfig> m_muxerCfg;
  HlsFileCallback m_hlsFileCallback;
  AudioCallback m_audioCallback;
  mutable std::mutex m_mediaMtx;
};

#endif // HEADLESS_ZOOM_BOT_MEDIA_CONTROLLER_H
