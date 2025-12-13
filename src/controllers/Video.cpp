#include "controllers/Video.h"

#include <algorithm>
#include <set>

#include "video/HlsMuxer.h"
#include "video/MediaEncodePipeline.h"
#include "video/VideoEncoder.h"

using namespace ZOOMSDK;

VideoController::VideoController(VideoConfig& config, const TimelineClock& timeline_clock)
    : config_(config), timeline_clock_(timeline_clock), is_recording_(false) {}

VideoController::~VideoController() noexcept { StopRecording(); }

void VideoController::LogStreamEvent(const StreamKey& key, const std::string& event,
                                     const std::string& level) {
  std::string msg = event + " " + config_.GetStreamType(key);
  ::LogStreamEvent(msg, level);
}

bool VideoController::ActivateStreamComponents(VideoStreamState& state, const StreamKey& key) {
  if (!state.renderer && !CreateVideoRenderer(state, key)) {
    LogStreamEvent(key, "Failed to create renderer", "error");
    return false;
  }

  if (!state.pipeline && !CreateVideoPipeline(state, key)) {
    LogStreamEvent(key, "Failed to create pipeline", "error");
    return false;
  }

  if (state.renderer) {
    SubscribeVideoRenderer(state, key);
  }

  return true;
}

StreamKey VideoController::GetHlsStreamKey(const StreamKey& key) const {
  // Camera streams already use Zoom user IDs as the key; keep them as-is.
  if (key.kind != StreamKind::kShare) {
    return key;
  }

  // For share streams, try to find the user_id from active share sources
  unsigned int user_id = key.id;  // Default to shareSourceID
  {
    std::lock_guard<std::mutex> lock(active_share_sources_mtx_);
    for (const auto& source : active_share_sources_) {
      if (source.shareSourceID == key.id) {
        user_id = source.userid;
        break;
      }
    }
  }

  return StreamKey{key.kind, user_id};
}

void VideoController::UpdateShareSources(const std::vector<ZoomSDKSharingSourceInfo>& sources) {
  // Store active share sources for GetHlsStreamKey lookups
  {
    std::lock_guard<std::mutex> lock(active_share_sources_mtx_);
    active_share_sources_ = sources;
  }

  std::set<unsigned int> new_source_ids;
  for (const auto& share_info : sources) {
    if (share_info.shareSourceID != 0) {
      new_source_ids.insert(share_info.shareSourceID);
    }
  }

  std::set<unsigned int> existing_ids;
  ForEachStream([&existing_ids](const StreamKey& key, const VideoStreamState&) {
    if (key.kind == StreamKind::kShare) {
      existing_ids.insert(key.id);
    }
  });

  std::vector<unsigned int> to_add, to_remove;
  std::set_difference(new_source_ids.begin(), new_source_ids.end(), existing_ids.begin(),
                      existing_ids.end(), std::back_inserter(to_add));
  std::set_difference(existing_ids.begin(), existing_ids.end(), new_source_ids.begin(),
                      new_source_ids.end(), std::back_inserter(to_remove));

  for (unsigned int share_id : to_add) {
    EnsureStream({StreamKind::kShare, share_id});
  }
  for (unsigned int share_id : to_remove) {
    DestroyStream({StreamKind::kShare, share_id});
  }
}

void VideoController::PushVideoFrame(StreamKind kind, unsigned int id, const char* y, const char* u,
                                     const char* v, unsigned int width, unsigned int height,
                                     uint64_t timestamp_ms) {
  std::shared_ptr<MediaEncodePipeline> pipeline;
  AccessStreamState({kind, id},
                    [&pipeline](const VideoStreamState& state) { pipeline = state.pipeline; });

  if (pipeline) {
    pipeline->PushVideoI420(y, u, v, width, height, timestamp_ms);
  }
}

void VideoController::PushAudioForHls(StreamKind kind, const uint8_t* pcm_data, size_t pcm_length,
                                      uint32_t sample_rate, uint32_t channels,
                                      uint64_t timestamp_ms) {
  std::vector<std::pair<StreamKey, std::shared_ptr<MediaEncodePipeline>>> pipelines;
  ForEachStream([&pipelines, kind](const StreamKey& key, const VideoStreamState& state) {
    if (state.pipeline && key.kind == kind) {
      pipelines.emplace_back(key, state.pipeline);
    }
  });

  for (auto& [key, pipeline] : pipelines) {
    pipeline->PushAudioPcm(pcm_data, pcm_length, sample_rate, channels, timestamp_ms);
  }
}

void VideoController::EnsureStream(const StreamKey& key) {
  bool is_new = EnsureStreamExists(key);
  if (!is_new) {
    return;
  }

  LogStreamEvent(key, "Prepared stream");

  if (is_recording_.load(std::memory_order_acquire)) {
    UpdateStreamState(
        key, [this, &key](VideoStreamState& state) { ActivateStreamComponents(state, key); });
  }
}

void VideoController::DestroyStream(const StreamKey& key) {
  VideoStreamState state;
  if (!RemoveStream(key, state)) {
    return;
  }

  if (state.pipeline) {
    state.pipeline->Stop();
  }

  LogStreamEvent(key, "Destroyed stream");
}

bool VideoController::CreateVideoRenderer(VideoStreamState& state, const StreamKey& key) {
  if (!video_delegate_factory_) {
    LogStreamEvent(key, "No video delegate factory set", "warn");
    return false;
  }

  auto raw_data_type =
      (key.kind == StreamKind::kCamera) ? RAW_DATA_TYPE_VIDEO : RAW_DATA_TYPE_SHARE;
  auto* stream_delegate = video_delegate_factory_(raw_data_type);
  if (!stream_delegate) {
    LogStreamEvent(key, "Factory returned null delegate", "error");
    return false;
  }

  IZoomSDKRenderer* renderer = nullptr;
  SDKError err = createRenderer(&renderer, stream_delegate);
  if (err != SDKERR_SUCCESS || !renderer) {
    LogStreamEvent(key, "Failed to create renderer (error: " + std::to_string(err) + ")", "error");
    delete stream_delegate;
    return false;
  }

  renderer->setRawDataResolution(ZoomSDKResolution_720P);

  if (is_recording_.load(std::memory_order_acquire)) {
    if (renderer->subscribe(key.id, raw_data_type) == SDKERR_SUCCESS) {
      state.renderer.reset(renderer);
      LogStreamEvent(key, "Subscribed");
      return true;
    } else {
      LogStreamEvent(key, "Failed to subscribe", "error");
      destroyRenderer(renderer);
      return false;
    }
  } else {
    state.renderer.reset(renderer);
    LogStreamEvent(key, "Created renderer (subscription deferred)", "info");
    return true;
  }
}

bool VideoController::CreateVideoPipeline(VideoStreamState& state, const StreamKey& key) {
  if (state.pipeline) {
    return true;
  }

  auto video_config = config_.CreateVideoConfig(key);
  auto audio_config = config_.CreateAudioConfig();
  auto muxer_config = config_.CreateHlsConfig(GetHlsStreamKey(key));
  auto hls_file_callback = config_.GetHlsFileCallback();

  if (!video_config || !audio_config || !muxer_config || !hls_file_callback) {
    LogStreamEvent(key, "Configs not set; cannot start pipeline", "warn");
    return false;
  }

  auto pipeline = std::make_shared<MediaEncodePipeline>();
  if (pipeline->Start(*video_config, *audio_config, *muxer_config, hls_file_callback)) {
    state.pipeline = pipeline;
    LogStreamEvent(key, "Started pipeline");
    return true;
  } else {
    LogStreamEvent(key, "Failed to start pipeline", "error");
    return false;
  }
}

void VideoController::SubscribeVideoRenderer(VideoStreamState& state, const StreamKey& key) {
  if (!state.renderer) {
    return;
  }

  if (state.renderer->getSubscribeId() != 0) {
    return;
  }

  auto raw_type = (key.kind == StreamKind::kCamera) ? RAW_DATA_TYPE_VIDEO : RAW_DATA_TYPE_SHARE;
  std::string type_str = (raw_type == RAW_DATA_TYPE_VIDEO) ? "VIDEO" : "SHARE";
  LogStreamEvent(key, "Subscribing with ID=" + std::to_string(key.id) + " type=" + type_str,
                 "info");

  SDKError sub_err = state.renderer->subscribe(key.id, raw_type);
  if (sub_err == SDKERR_SUCCESS) {
    LogStreamEvent(
        key, "Subscribed (subscribeId=" + std::to_string(state.renderer->getSubscribeId()) + ")");
  } else {
    LogStreamEvent(key, "Failed to subscribe (error=" + std::to_string(sub_err) + ")", "error");
  }
}

void VideoController::UpdateCameraStatus(unsigned int user_id, bool video_on) {
  StreamKey key{StreamKind::kCamera, user_id};
  if (video_on) {
    EnsureStream(key);
  } else {
    DestroyStream(key);
  }
}

void VideoController::StartRecording() {
  is_recording_.store(true, std::memory_order_release);

  // Activate all existing streams
  auto keys = GetAllKeys();
  for (const auto& key : keys) {
    UpdateStreamState(
        key, [this, &key](VideoStreamState& state) { ActivateStreamComponents(state, key); });
  }
}

void VideoController::StopRecording() {
  if (!is_recording_.load(std::memory_order_acquire)) {
    return;
  }
  is_recording_.store(false, std::memory_order_release);

  auto streams = ClearAllStreams();
  for (auto& [key, state] : streams) {
    if (state.pipeline) {
      state.pipeline->Stop();
    }
    // Renderer and delegate destroyed automatically by unique_ptr with custom deleter
  }
}
