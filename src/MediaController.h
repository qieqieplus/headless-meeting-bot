#ifndef HEADLESS_ZOOM_BOT_MEDIA_CONTROLLER_H
#define HEADLESS_ZOOM_BOT_MEDIA_CONTROLLER_H

#include <memory>
#include <mutex>
#include <functional>
#include <vector>

#include "zoom_sdk_def.h"
#include "meeting_service_components/meeting_sharing_interface.h"
#include "meeting_service_components/meeting_audio_interface.h"
#include "rawdata/zoom_rawdata_api.h"
#include "rawdata/rawdata_audio_helper_interface.h"
#include "rawdata/rawdata_renderer_interface.h"

// Forward declarations
struct FFmpegEncoderConfig;
struct HlsMuxerConfig;
class VideoEncodePipeline;

/**
 * MediaController encapsulates all media handling (audio/video) for a Meeting.
 * Manages video encoding pipeline lifecycle, HLS configuration, and frame dispatch.
 */
class MediaController {
public:
    using HlsFileCallback = std::function<void(const char* filename,
                                               const uint8_t* data,
                                               size_t size,
                                               int is_playlist,
                                               uint64_t sequence)>;

    MediaController();
    ~MediaController();

    // Set audio/video delegates (must be called before starting recording)
    void setAudioDelegate(ZOOMSDK::IZoomSDKAudioRawDataDelegate* delegate);
    void setVideoDelegate(ZOOMSDK::IZoomSDKRendererDelegate* delegate);
    
    ZOOMSDK::IZoomSDKAudioRawDataDelegate* getAudioDelegate() const { return m_audioDelegate; }
    ZOOMSDK::IZoomSDKRendererDelegate* getVideoDelegate() const { return m_videoDelegate; }

    // HLS video configuration
    void setHlsVideoParams(const FFmpegEncoderConfig& encCfg,
                          const HlsMuxerConfig& muxCfg,
                          HlsFileCallback cb);
    void clearHlsVideoParams();

    // Start/stop raw recording (handles audio/video subscription)
    ZOOMSDK::SDKError startMedia(bool useRawAudio, bool useRawVideo,
                          const std::vector<ZOOMSDK::ZoomSDKSharingSourceInfo>& currentShares = {});
    ZOOMSDK::SDKError stopMedia();

    // Share event handlers (called by Meeting's share event)
    void onShareStart(const ZOOMSDK::ZoomSDKSharingSourceInfo& shareInfo);
    void onShareEnd(const ZOOMSDK::ZoomSDKSharingSourceInfo& shareInfo);

    // Pipeline lifecycle (tied to recording state)
    void startVideoPipeline();
    void stopVideoPipeline();

    // Video frame dispatch
    void pushVideoI420(const char* y,
                      const char* u,
                      const char* v,
                      unsigned int width,
                      unsigned int height,
                      unsigned long long timestampMs);

    // Encoder control
    void requestVideoEncoderIDR();

    // Thread-safe pipeline access
    std::shared_ptr<VideoEncodePipeline> getVideoPipelineShared() const;

    // Query state
    bool isRecording() const { return m_isRecording; }

private:
    ZOOMSDK::SDKError subscribeShare(const ZOOMSDK::ZoomSDKSharingSourceInfo& shareInfo);
    ZOOMSDK::SDKError unSubscribeShare(const ZOOMSDK::ZoomSDKSharingSourceInfo& shareInfo);

    // Audio/video helpers and delegates
    ZOOMSDK::IZoomSDKAudioRawDataHelper* m_audioHelper;
    ZOOMSDK::IZoomSDKAudioRawDataDelegate* m_audioDelegate;
    ZOOMSDK::IZoomSDKRenderer* m_videoHelper;
    ZOOMSDK::IZoomSDKRendererDelegate* m_videoDelegate;
    
    // Synchronizes access to share state and renderer subscription
    mutable std::mutex m_shareMtx;

    // Share tracking (most-recent share on top)
    std::vector<unsigned int> m_shareSourceIds = {};

    // Recording state
    bool m_isRecording;

    // Video pipeline state
    std::shared_ptr<VideoEncodePipeline> m_videoPipeline;
    std::unique_ptr<FFmpegEncoderConfig> m_encoderCfg;
    std::unique_ptr<HlsMuxerConfig> m_muxerCfg;
    HlsFileCallback m_hlsFileCallback;
    mutable std::mutex m_videoMtx;
};

#endif // HEADLESS_ZOOM_BOT_MEDIA_CONTROLLER_H

