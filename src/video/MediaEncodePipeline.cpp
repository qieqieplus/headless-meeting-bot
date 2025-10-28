#include "MediaEncodePipeline.h"

#include "util/Logger.h"
#include <algorithm>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace {
// Constants
const int DEFAULT_AUDIO_FRAME_SIZE = 1024;
const int DEFAULT_VIDEO_POOL_SIZE = 4;
const int DEFAULT_AUDIO_POOL_SIZE = 8;
} // namespace

MediaEncodePipeline::MediaEncodePipeline() = default;
MediaEncodePipeline::~MediaEncodePipeline() { stop(); }

bool MediaEncodePipeline::start(const VideoEncoderConfig &vidEncCfg,
                                const AudioEncoderConfig &audEncCfg,
                                const HlsMuxerConfig &muxCfg,
                                HlsFileCallback hlsCb) {
  stop();
  videoEncoderCfg = vidEncCfg;
  audioEncoderCfg = audEncCfg;
  muxerCfg = muxCfg;
  fileCallback = std::move(hlsCb);

  // Initialize video pool
  videoPool.clear();
  videoPool.resize(DEFAULT_VIDEO_POOL_SIZE);
  videoHead = videoTail = videoSize = 0;

  // Initialize audio pool
  audioPool.clear();
  audioPool.resize(DEFAULT_AUDIO_POOL_SIZE);
  audioHead = audioTail = audioSize = 0;

  running = true;
  worker = std::thread(&MediaEncodePipeline::workerLoop, this);
  return true;
}

void MediaEncodePipeline::stop() {
  if (running.exchange(false)) {
    videoCv.notify_all();
    audioCv.notify_all();
    if (worker.joinable())
      worker.join();
  }
  muxer.shutdown();
  videoEncoder.shutdown();
  audioEncoder.shutdown();
}

void MediaEncodePipeline::pushVideoI420(const char *y, const char *u,
                                        const char *v, unsigned int width,
                                        unsigned int height,
                                        unsigned long long timestampMs) {
  if (!running)
    return;
  std::unique_lock<std::mutex> lock(videoMtx);
  if (videoSize == videoPool.size()) {
    Logger::getInstance().warn("Video queue full: dropping frame");
    return;
  }
  VideoFrame &f = videoPool[videoHead];
  f.width = width;
  f.height = height;
  f.ts = timestampMs;
  size_t ySize = static_cast<size_t>(width) * height;
  size_t cW = width / 2;
  size_t cH = height / 2;
  size_t cSize = cW * cH;
  f.y.resize(ySize);
  f.u.resize(cSize);
  f.v.resize(cSize);
  std::memcpy(f.y.data(), y, ySize);
  std::memcpy(f.u.data(), u, cSize);
  std::memcpy(f.v.data(), v, cSize);
  f.occupied = true;
  videoHead = (videoHead + 1) % videoPool.size();
  ++videoSize;
  lock.unlock();
  videoCv.notify_one();
}

void MediaEncodePipeline::pushAudioPCM(const uint8_t *pcmData, size_t pcmLength,
                                       uint32_t sampleRate, uint32_t channels,
                                       uint64_t timestampMs) {
  if (!running)
    return;
  std::unique_lock<std::mutex> lock(audioMtx);
  if (audioSize == audioPool.size()) {
    Logger::getInstance().warn("Audio queue full: dropping frame");
    return;
  }
  AudioFrame &f = audioPool[audioHead];
  f.data.resize(pcmLength);
  std::memcpy(f.data.data(), pcmData, pcmLength);
  f.sampleRate = sampleRate;
  f.channels = channels;
  f.ts = timestampMs;
  f.occupied = true;
  audioHead = (audioHead + 1) % audioPool.size();
  ++audioSize;
  lock.unlock();
  audioCv.notify_one();
}

void MediaEncodePipeline::requestIDR() { videoEncoder.requestIDR(); }

bool MediaEncodePipeline::ensureVideoEncoder(unsigned int w, unsigned int h) {
  if (videoEncoder.getWidth() == static_cast<int>(w) &&
      videoEncoder.getHeight() == static_cast<int>(h)) {
    return true;
  }

  if (videoEncoder.getWidth() == 0) {
    VideoEncoderConfig cfg = videoEncoderCfg;
    cfg.width = static_cast<int>(w);
    cfg.height = static_cast<int>(h);

    if (!videoEncoder.initialize(cfg)) {
      return false;
    }

    // Initialize muxer
    HlsMuxerConfig muxCfg = muxerCfg;
    muxCfg.width = static_cast<int>(w);
    muxCfg.height = static_cast<int>(h);
    muxCfg.fps = cfg.fps;

    if (!muxer.initialize(muxCfg, fileCallback)) {
      return false;
    }

    // Copy video codec parameters from encoder to muxer stream
    AVCodecParameters *videoCodecParams = muxer.getVideoCodecParams();
    AVCodecContext *videoCodecCtx = videoEncoder.getCodecContext();
    if (videoCodecParams && videoCodecCtx) {
      avcodec_parameters_from_context(videoCodecParams, videoCodecCtx);
      videoCodecParams->format = videoCodecCtx->pix_fmt;
    }

    // Initialize audio encoder (if not already initialized)
    if (!ensureAudioEncoder()) {
      Logger::getInstance().warn("Failed to initialize audio encoder");
    }

    // Configure audio stream now that muxer is ready
    if (audioEncoder.getSampleRate() > 0) {
      if (!muxer.configureAudioStream(audioEncoder.getCodecContext())) {
        Logger::getInstance().error("Failed to configure audio stream");
        return false;
      }
    }

    if (!muxer.start()) {
      return false;
    }

    return true;
  }

  // Reinitialize if dimensions changed
  VideoEncoderConfig cfg = videoEncoderCfg;
  cfg.width = static_cast<int>(w);
  cfg.height = static_cast<int>(h);
  return videoEncoder.reinitialize(cfg);
}

bool MediaEncodePipeline::ensureAudioEncoder() {
  if (audioEncoder.getSampleRate() > 0) {
    return true; // Already initialized
  }

  if (!audioEncoder.initialize(audioEncoderCfg)) {
    return false;
  }

  // Only configure audio stream if muxer is ready (has been initialized)
  // This prevents configuring audio stream before video encoder sets up the
  // muxer
  if (muxer.getAudioCodecParams() != nullptr) {
    if (!muxer.configureAudioStream(audioEncoder.getCodecContext())) {
      Logger::getInstance().error("Failed to configure audio stream in muxer");
      return false;
    }
  }

  return true;
}

void MediaEncodePipeline::workerLoop() {
  while (running) {
    bool hasVideo = false;
    bool hasAudio = false;
    VideoFrame vf;
    AudioFrame af;

    // Check for video frame
    {
      std::unique_lock<std::mutex> lock(videoMtx);
      if (videoSize > 0) {
        VideoFrame &slot = videoPool[videoTail];
        vf = slot;           // copy out
        slot = VideoFrame{}; // release memory for reuse
        videoTail = (videoTail + 1) % videoPool.size();
        --videoSize;
        hasVideo = true;
      }
    }

    // Check for audio frame
    {
      std::unique_lock<std::mutex> lock(audioMtx);
      if (audioSize > 0) {
        AudioFrame &slot = audioPool[audioTail];
        af = slot;           // copy out
        slot = AudioFrame{}; // release memory for reuse
        audioTail = (audioTail + 1) % audioPool.size();
        --audioSize;
        hasAudio = true;
      }
    }

    // Process video frame
    if (hasVideo) {
      if (!ensureVideoEncoder(vf.width, vf.height)) {
        Logger::getInstance().error("HLS video encoder initialization failed");
      } else {
        // Only encode video if muxer is ready (header written)
        if (muxer.getVideoCodecParams() != nullptr) {
          // Encode frame (timestamp in microseconds)
          int64_t ptsUs = static_cast<int64_t>(vf.ts) * 1000;
          AVPacket *pkt =
              videoEncoder.encodeI420(vf.y.data(), vf.u.data(), vf.v.data(),
                                      vf.width, vf.height, ptsUs);
          if (pkt) {
            if (pkt->duration == 0) {
              pkt->duration = videoEncoder.getFrameDuration();
            }
            // Write packet to HLS muxer
            bool written = muxer.writeVideoPacket(pkt);
            av_packet_unref(pkt);

            if (!written) {
              Logger::getInstance().error(
                  "Failed to write video packet to HLS muxer");
            }
          }
        }
      }
    }

    // Process audio frame
    if (hasAudio) {
      if (!ensureAudioEncoder()) {
        Logger::getInstance().error("HLS audio encoder initialization failed");
      } else {
        // Only encode audio if muxer is ready (header written)
        if (muxer.getAudioCodecParams() != nullptr) {
          int frameSize = audioEncoder.getFrameSize();
          if (frameSize <= 0) {
            frameSize = DEFAULT_AUDIO_FRAME_SIZE;
          }
          int samples = static_cast<int>(af.data.size() /
                                         (sizeof(int16_t) * af.channels));
          int64_t ptsUs = static_cast<int64_t>(af.ts) * 1000;
          AVPacket *pkt =
              audioEncoder.encodePCM(af.data.data(), af.data.size(),
                                     af.sampleRate, af.channels, ptsUs);
          if (pkt) {
            if (pkt->duration == 0) {
              pkt->duration = audioEncoder.getFrameDuration();
            }
            // Write packet to HLS muxer
            bool written = muxer.writeAudioPacket(pkt);
            av_packet_unref(pkt);

            if (!written) {
              Logger::getInstance().error(
                  "Failed to write audio packet to HLS muxer");
            }
          }
        }
      }
    }

    // If no frames were available, wait for notification
    if (!hasVideo && !hasAudio) {
      std::unique_lock<std::mutex> vlock(videoMtx);
      videoCv.wait_for(vlock, std::chrono::milliseconds(20),
                       [&] { return !running || videoSize > 0; });
      if (!running)
        break;
      std::unique_lock<std::mutex> alock(audioMtx);
      audioCv.wait_for(alock, std::chrono::milliseconds(20),
                       [&] { return !running || audioSize > 0; });
    }
  }

  Logger::getInstance().info("MediaEncodePipeline worker loop exited");
}
