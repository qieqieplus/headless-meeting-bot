#include "MediaController.h"
#include "video/VideoEncodePipeline.h"
#include "video/FFmpegEncoder.h"
#include "video/HlsMuxer.h"
#include "util/Logger.h"
#include "util/Checks.h"

#include <algorithm>

using namespace ZOOMSDK;

MediaController::MediaController()
    : m_audioHelper(nullptr)
    , m_audioDelegate(nullptr)
    , m_videoHelper(nullptr)
    , m_videoDelegate(nullptr)
    , m_isRecording(false) {
}

MediaController::~MediaController() {
    stopMedia();
}

void MediaController::setAudioDelegate(IZoomSDKAudioRawDataDelegate* delegate) {
    m_audioDelegate = delegate;
}

void MediaController::setVideoDelegate(IZoomSDKRendererDelegate* delegate) {
    m_videoDelegate = delegate;
}

void MediaController::setHlsVideoParams(const FFmpegEncoderConfig& encCfg,
                                        const HlsMuxerConfig& muxCfg,
                                        HlsFileCallback cb) {
    std::lock_guard<std::mutex> lock(m_videoMtx);
    m_encoderCfg = std::make_unique<FFmpegEncoderConfig>(encCfg);
    m_muxerCfg = std::make_unique<HlsMuxerConfig>(muxCfg);
    m_hlsFileCallback = std::move(cb);
}

void MediaController::clearHlsVideoParams() {
    std::lock_guard<std::mutex> lock(m_videoMtx);
    m_encoderCfg.reset();
    m_muxerCfg.reset();
    m_hlsFileCallback = nullptr;
}

SDKError MediaController::startMedia(bool useRawAudio, bool useRawVideo,
                                        const std::vector<ZoomSDKSharingSourceInfo>& currentShares) {
    if (m_isRecording) return SDKERR_SUCCESS;
    m_isRecording = true;
    
    {
        std::lock_guard<std::mutex> lock(m_shareMtx);
        m_shareSourceIds.clear();
        // Setup audio
        if (useRawAudio && m_audioDelegate) {
            m_audioHelper = GetAudioRawdataHelper();
            ASSERT_NOT_NULL(m_audioHelper);
            ZOOM_ERR_CHECK(m_audioHelper->subscribe(m_audioDelegate), "subscribe to raw audio");
        }

        // Setup video (shared screen capture)
        if (useRawVideo) {
            ASSERT_NOT_NULL(m_videoDelegate);
            ZOOM_ERR_CHECK(createRenderer(&m_videoHelper, m_videoDelegate), "create renderer");

            m_videoHelper->setRawDataResolution(ZoomSDKResolution_720P);
            // Subscribe to any currently active shares
            for (const auto& shareInfo : currentShares) {
                if (shareInfo.contentType == SHARE_TYPE_DATA) {
                    
                    if (subscribeShare(shareInfo) == SDKERR_SUCCESS) {
                        break;
                    }
                }
            }
        }
    }
    
    // Start video pipeline if HLS is configured
    startVideoPipeline();
    
    Util::Logger::getInstance().success("Raw recording started");
    return SDKERR_SUCCESS;
}

SDKError MediaController::stopMedia() {
    if (!m_isRecording) return SDKERR_SUCCESS;
    m_isRecording = false;

    stopVideoPipeline();

    {
        std::lock_guard<std::mutex> lock(m_shareMtx);
        m_shareSourceIds.clear();
    

        if (m_audioHelper) {
            m_audioHelper->unSubscribe();
            m_audioHelper = nullptr;
        }
        
        if (m_videoHelper) {
            m_videoHelper->unSubscribe();
            destroyRenderer(m_videoHelper);
            m_videoHelper = nullptr;
        }
    }

    Util::Logger::getInstance().success("Raw recording stopped");
    return SDKERR_SUCCESS;
}

void MediaController::onShareStart(const ZoomSDKSharingSourceInfo& shareInfo) {
    if (shareInfo.contentType == SHARE_TYPE_DATA) {
        std::lock_guard<std::mutex> lock(m_shareMtx);
        subscribeShare(shareInfo);
    }
}

void MediaController::onShareEnd(const ZoomSDKSharingSourceInfo& shareInfo) {
    if (shareInfo.contentType == SHARE_TYPE_DATA) {
        std::lock_guard<std::mutex> lock(m_shareMtx);
        unSubscribeShare(shareInfo);
    }
}

void MediaController::startVideoPipeline() {
    std::shared_ptr<VideoEncodePipeline> pipeline;
    FFmpegEncoderConfig enc;
    HlsMuxerConfig mux;
    HlsFileCallback cb;

    {
        std::lock_guard<std::mutex> lock(m_videoMtx);
        if (m_videoPipeline) return;
        if (!m_encoderCfg || !m_muxerCfg || !m_hlsFileCallback) return;

        enc = *m_encoderCfg;
        mux = *m_muxerCfg;
        cb = m_hlsFileCallback;
        pipeline = std::make_shared<VideoEncodePipeline>();
        m_videoPipeline = pipeline;
    }

    if (pipeline->start(enc, mux, cb)) {
        Util::Logger::getInstance().success("Video pipeline started");
        return;
    }

    Util::Logger::getInstance().error("Failed to start video pipeline");
    std::lock_guard<std::mutex> lock(m_videoMtx);
    if (m_videoPipeline == pipeline) {
        m_videoPipeline.reset();
    }
}

void MediaController::stopVideoPipeline() {
    std::shared_ptr<VideoEncodePipeline> pipeline;
    {
        std::lock_guard<std::mutex> lock(m_videoMtx);
        pipeline = m_videoPipeline;
        m_videoPipeline.reset();
    }
    if (pipeline) {
        pipeline->stop();
        Util::Logger::getInstance().success("Video pipeline stopped");
    }
}

void MediaController::pushVideoI420(const char* y,
                                    const char* u,
                                    const char* v,
                                    unsigned int width,
                                    unsigned int height,
                                    unsigned long long timestampMs) { 
    if (auto pipeline = getVideoPipelineShared()) {
        pipeline->pushI420(y, u, v, width, height, timestampMs);
    }
}

void MediaController::requestVideoEncoderIDR() {
    if (auto pipeline = getVideoPipelineShared()) {
        pipeline->requestIDR();
    }
}

std::shared_ptr<VideoEncodePipeline> MediaController::getVideoPipelineShared() const {
    std::lock_guard<std::mutex> lock(m_videoMtx);
    return m_videoPipeline;
}

static SDKError subscribeTo(IZoomSDKRenderer* videoHelper, std::vector<unsigned int>& shareSourceIds) {
    if (!videoHelper) {
        // Renderer not ready; sharing state will be picked up when recording starts
        return SDKERR_SUCCESS;
    }

    videoHelper->unSubscribe();
    
    if (shareSourceIds.empty()) {
        return SDKERR_SUCCESS;
    }

    auto sourceId = shareSourceIds.back();
    ZOOM_ERR_CHECK(videoHelper->subscribe(sourceId, RAW_DATA_TYPE_SHARE), "Subscribe to share source " + std::to_string(sourceId));
    
    Util::Logger::getInstance().success("Subscribed to share source " + std::to_string(sourceId));
    return SDKERR_SUCCESS;
}

SDKError MediaController::subscribeShare(const ZoomSDKSharingSourceInfo& shareInfo) {
    const unsigned int sourceId = shareInfo.shareSourceID;
    const bool subscribed = !m_shareSourceIds.empty() && (sourceId == m_shareSourceIds.back());
    if (subscribed) {
        return SDKERR_SUCCESS;
    }

    auto it = std::find(m_shareSourceIds.begin(), m_shareSourceIds.end(), sourceId);
    if (it != m_shareSourceIds.end()) {
        m_shareSourceIds.erase(it);
    }
    m_shareSourceIds.push_back(sourceId);
    return subscribeTo(m_videoHelper, m_shareSourceIds);
}

SDKError MediaController::unSubscribeShare(const ZoomSDKSharingSourceInfo& shareInfo) {   
    const unsigned int sourceId = shareInfo.shareSourceID;
    const bool subscribed = !m_shareSourceIds.empty() && (sourceId == m_shareSourceIds.back());

    auto it = std::find(m_shareSourceIds.begin(), m_shareSourceIds.end(), sourceId);
    if (it == m_shareSourceIds.end()) {
        return SDKERR_UNKNOWN;
    }
    m_shareSourceIds.erase(it);

    if (!subscribed) {
        return SDKERR_SUCCESS;
    }
    return subscribeTo(m_videoHelper, m_shareSourceIds);
}
