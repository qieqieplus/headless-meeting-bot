#include "MediaController.h"
#include "util/Checks.h"
#include "util/Logger.h"
#include "video/AudioEncoder.h"
#include "video/HlsMuxer.h"
#include "video/MediaEncodePipeline.h"
#include "video/VideoEncoder.h"

#include <algorithm>

using namespace ZOOMSDK;

MediaController::MediaController()
    : m_audioHelper(nullptr), m_audioDelegate(nullptr), m_videoHelper(nullptr),
      m_videoDelegate(nullptr), m_isRecording(false) {}

MediaController::~MediaController() { stopMedia(); }

void MediaController::setAudioDelegate(IZoomSDKAudioRawDataDelegate *delegate) {
  m_audioDelegate = delegate;
}

void MediaController::setVideoDelegate(IZoomSDKRendererDelegate *delegate) {
  m_videoDelegate = delegate;
}

void MediaController::setHlsMediaParams(const VideoEncoderConfig &videoEncCfg,
                                        const AudioEncoderConfig &audioEncCfg,
                                        const HlsMuxerConfig &muxCfg,
                                        HlsFileCallback cb) {
  std::lock_guard<std::mutex> lock(m_mediaMtx);
  m_videoEncoderCfg = std::make_unique<VideoEncoderConfig>(videoEncCfg);
  m_audioEncoderCfg = std::make_unique<AudioEncoderConfig>(audioEncCfg);
  m_muxerCfg = std::make_unique<HlsMuxerConfig>(muxCfg);
  m_hlsFileCallback = std::move(cb);
}

void MediaController::clearHlsMediaParams() {
  std::lock_guard<std::mutex> lock(m_mediaMtx);
  m_videoEncoderCfg.reset();
  m_audioEncoderCfg.reset();
  m_muxerCfg.reset();
  m_hlsFileCallback = nullptr;
  m_audioCallback = nullptr;
}

SDKError MediaController::startMedia(
    bool useRawAudio, bool useRawVideo,
    const std::vector<ZoomSDKSharingSourceInfo> &currentShares) {
  if (m_isRecording)
    return SDKERR_SUCCESS;
  m_isRecording = true;

  {
    std::lock_guard<std::mutex> lock(m_shareMtx);
    m_shareSourceIds.clear();
    // Setup audio
    if (useRawAudio && m_audioDelegate) {
      m_audioHelper = GetAudioRawdataHelper();
      ASSERT_NOT_NULL(m_audioHelper);
      ZOOM_ERR_CHECK(m_audioHelper->subscribe(m_audioDelegate),
                     "subscribe to raw audio");
    }

    // Setup video (shared screen capture)
    if (useRawVideo) {
      ASSERT_NOT_NULL(m_videoDelegate);
      ZOOM_ERR_CHECK(createRenderer(&m_videoHelper, m_videoDelegate),
                     "create renderer");

      m_videoHelper->setRawDataResolution(ZoomSDKResolution_720P);
      // Subscribe to any currently active shares
      for (const auto &shareInfo : currentShares) {
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

  Logger::getInstance().success("Raw recording started");
  return SDKERR_SUCCESS;
}

SDKError MediaController::stopMedia() {
  if (!m_isRecording)
    return SDKERR_SUCCESS;
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

  Logger::getInstance().success("Raw recording stopped");
  return SDKERR_SUCCESS;
}

void MediaController::onShareStart(const ZoomSDKSharingSourceInfo &shareInfo) {
  if (shareInfo.contentType == SHARE_TYPE_DATA) {
    std::lock_guard<std::mutex> lock(m_shareMtx);
    subscribeShare(shareInfo);
  }
}

void MediaController::onShareEnd(const ZoomSDKSharingSourceInfo &shareInfo) {
  if (shareInfo.contentType == SHARE_TYPE_DATA) {
    std::lock_guard<std::mutex> lock(m_shareMtx);
    unSubscribeShare(shareInfo);
  }
}

void MediaController::startVideoPipeline() {
  std::shared_ptr<MediaEncodePipeline> pipeline;
  VideoEncoderConfig videoEnc;
  AudioEncoderConfig audioEnc;
  HlsMuxerConfig mux;
  HlsFileCallback cb;

  {
    std::lock_guard<std::mutex> lock(m_mediaMtx);
    if (m_mediaPipeline)
      return;
    if (!m_videoEncoderCfg || !m_audioEncoderCfg || !m_muxerCfg ||
        !m_hlsFileCallback)
      return;

    videoEnc = *m_videoEncoderCfg;
    audioEnc = *m_audioEncoderCfg;
    mux = *m_muxerCfg;
    cb = m_hlsFileCallback;
    pipeline = std::make_shared<MediaEncodePipeline>();
    m_mediaPipeline = pipeline;
  }

  if (pipeline->start(videoEnc, audioEnc, mux, cb)) {
    Logger::getInstance().success("Media pipeline started");
    return;
  }

  Logger::getInstance().error("Failed to start media pipeline");
  std::lock_guard<std::mutex> lock(m_mediaMtx);
  if (m_mediaPipeline == pipeline) {
    m_mediaPipeline.reset();
  }
}

void MediaController::stopVideoPipeline() {
  std::shared_ptr<MediaEncodePipeline> pipeline;
  {
    std::lock_guard<std::mutex> lock(m_mediaMtx);
    pipeline = m_mediaPipeline;
    m_mediaPipeline.reset();
  }
  if (pipeline) {
    pipeline->stop();
    Logger::getInstance().success("Media pipeline stopped");
  }
}

void MediaController::pushVideoI420(const char *y, const char *u, const char *v,
                                    unsigned int width, unsigned int height,
                                    unsigned long long timestampMs) {
  if (auto pipeline = getMediaPipelineShared()) {
    pipeline->pushVideoI420(y, u, v, width, height, timestampMs);
  }
}

void MediaController::pushAudioPCM(const uint8_t *pcmData, size_t pcmLength,
                                   uint32_t sampleRate, uint32_t channels,
                                   uint64_t timestampMs) {
  if (auto pipeline = getMediaPipelineShared()) {
    pipeline->pushAudioPCM(pcmData, pcmLength, sampleRate, channels,
                           timestampMs);
  }
}

void MediaController::dispatchAudio(const uint8_t *pcmData, size_t pcmLength,
                                    uint32_t sampleRate, uint32_t channels,
                                    int audioType, uint32_t userId,
                                    uint64_t timestampMs) {
  if (m_audioCallback && pcmData && pcmLength > 0) {
    m_audioCallback(pcmData, pcmLength, sampleRate, channels, audioType, userId,
                    timestampMs);
  }
}

void MediaController::requestVideoEncoderIDR() {
  if (auto pipeline = getMediaPipelineShared()) {
    pipeline->requestIDR();
  }
}

std::shared_ptr<MediaEncodePipeline>
MediaController::getMediaPipelineShared() const {
  std::lock_guard<std::mutex> lock(m_mediaMtx);
  return m_mediaPipeline;
}

static SDKError subscribeTo(IZoomSDKRenderer *videoHelper,
                            std::vector<unsigned int> &shareSourceIds) {
  if (!videoHelper) {
    // Renderer not ready; sharing state will be picked up when recording starts
    return SDKERR_SUCCESS;
  }

  videoHelper->unSubscribe();

  if (shareSourceIds.empty()) {
    return SDKERR_SUCCESS;
  }

  auto sourceId = shareSourceIds.back();
  ZOOM_ERR_CHECK(videoHelper->subscribe(sourceId, RAW_DATA_TYPE_SHARE),
                 "Subscribe to share source " + std::to_string(sourceId));

  Logger::getInstance().success("Subscribed to share source " +
                                std::to_string(sourceId));
  return SDKERR_SUCCESS;
}

SDKError
MediaController::subscribeShare(const ZoomSDKSharingSourceInfo &shareInfo) {
  const unsigned int sourceId = shareInfo.shareSourceID;
  const bool subscribed =
      !m_shareSourceIds.empty() && (sourceId == m_shareSourceIds.back());
  if (subscribed) {
    return SDKERR_SUCCESS;
  }

  auto it =
      std::find(m_shareSourceIds.begin(), m_shareSourceIds.end(), sourceId);
  if (it != m_shareSourceIds.end()) {
    m_shareSourceIds.erase(it);
  }
  m_shareSourceIds.push_back(sourceId);
  return subscribeTo(m_videoHelper, m_shareSourceIds);
}

SDKError
MediaController::unSubscribeShare(const ZoomSDKSharingSourceInfo &shareInfo) {
  const unsigned int sourceId = shareInfo.shareSourceID;
  const bool subscribed =
      !m_shareSourceIds.empty() && (sourceId == m_shareSourceIds.back());

  auto it =
      std::find(m_shareSourceIds.begin(), m_shareSourceIds.end(), sourceId);
  if (it == m_shareSourceIds.end()) {
    return SDKERR_UNKNOWN;
  }
  m_shareSourceIds.erase(it);

  if (!subscribed) {
    return SDKERR_SUCCESS;
  }
  return subscribeTo(m_videoHelper, m_shareSourceIds);
}
