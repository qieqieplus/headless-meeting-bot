#include "MediaController.h"

#include <algorithm>

#include "util/Checks.h"
#include "util/Logger.h"
#include "video/AudioEncoder.h"
#include "video/HlsMuxer.h"
#include "video/MediaEncodePipeline.h"
#include "video/VideoEncoder.h"

using namespace ZOOMSDK;

MediaController::MediaController(const std::string& meeting_id)
    : config_(meeting_id),
      is_recording_(false),
      recording_controller_(nullptr),
      use_raw_audio_(false),
      use_raw_video_(false) {
  audio_helper_ = nullptr;
}

MediaController::~MediaController() noexcept {
  CleanupRecording();
  StopMedia();
}

void MediaController::SetAudioDelegate(std::unique_ptr<IZoomSDKAudioRawDataDelegate> delegate) {
  audio_delegate_ = std::move(delegate);
}

SDKError MediaController::StartMedia(bool use_raw_audio, bool use_raw_video,
                                     const std::vector<ZoomSDKSharingSourceInfo>& initial_shares) {
  const bool was_recording = is_recording_.load(std::memory_order_acquire);

  if (use_raw_audio && audio_helper_ == nullptr) {
    ASSERT_NOT_NULL(audio_delegate_);
    audio_helper_ = GetAudioRawdataHelper();
    ASSERT_NOT_NULL(audio_helper_);
    ZOOM_ERR_CHECK(audio_helper_->subscribe(audio_delegate_.get()), "subscribe to raw audio");
  }
  // Prime existing share streams that may already be active when we join
  if (use_raw_video && !initial_shares.empty()) {
    UpdateShareSources(initial_shares);
  }

  // This must be set before subscribing to renderers
  is_recording_.store(true, std::memory_order_release);

  // Create renderers, start pipelines, and subscribe for any prepared streams
  if (use_raw_video) {
    IterateStream([this](const StreamKey& key, StreamState& state) {
      // Create renderer if not already created
      if (!state.renderer) {
        CreateRendererForStream(state, key);
      }

      // Start pipeline if not already started
      if (!state.pipeline) {
        CreatePipelineForStream(state, key);
      }

      // Subscribe renderer if not already subscribed
      SubscribeRendererForStream(state, key);
    });
  }

  if (!was_recording) {
    Logger::GetInstance().Success("Raw recording started");
  } else {
    Logger::GetInstance().Info("Raw recording refreshed");
  }
  return SDKERR_SUCCESS;
}

SDKError MediaController::StopMedia() {
  if (!is_recording_.load(std::memory_order_acquire)) {
    return SDKERR_SUCCESS;
  }
  is_recording_.store(false, std::memory_order_release);

  // Clean up audio resources
  if (audio_helper_) {
    audio_helper_->unSubscribe();
    audio_helper_ = nullptr;
  }
  audio_delegate_.reset();

  // Stop all stream pipelines
  std::unordered_map<StreamKey, StreamState> streams;
  {
    std::lock_guard<std::mutex> lock(media_mtx_);
    streams = std::move(streams_);
    streams_.clear();
  }

  // Clean up all streams
  for (auto& [key, state] : streams) {
    DestroyStream(key);
  }

  Logger::GetInstance().Success("Raw recording stopped");
  timeline_clock_.Reset();
  return SDKERR_SUCCESS;
}

void MediaController::UpdateShareSources(const std::vector<ZoomSDKSharingSourceInfo>& sources) {
  // Build set of new share source IDs
  std::vector<unsigned int> new_source_ids;
  new_source_ids.reserve(sources.size());
  for (const auto& share_info : sources) {
    // Accept all active share sources regardless of content type
    if (share_info.shareSourceID != 0) {
      new_source_ids.push_back(share_info.shareSourceID);
    }
  }

  // Find existing share stream IDs
  std::vector<unsigned int> existing_ids;
  IterateStream([&existing_ids](const StreamKey& key, StreamState&) {
    if (key.kind == StreamKind::kShare) {
      existing_ids.push_back(key.id);
    }
  });

  // Find shares to add (in new but not in existing)
  std::vector<unsigned int> to_add;
  for (unsigned int new_id : new_source_ids) {
    if (std::find(existing_ids.begin(), existing_ids.end(), new_id) == existing_ids.end()) {
      to_add.push_back(new_id);
    }
  }

  // Find shares to remove (in existing but not in new)
  std::vector<unsigned int> to_remove;
  for (unsigned int existing_id : existing_ids) {
    if (std::find(new_source_ids.begin(), new_source_ids.end(), existing_id) ==
        new_source_ids.end()) {
      to_remove.push_back(existing_id);
    }
  }

  // Create new share streams
  for (unsigned int share_id : to_add) {
    EnsureStream({StreamKind::kShare, share_id});
  }

  // Destroy removed share streams
  for (unsigned int share_id : to_remove) {
    DestroyStream({StreamKind::kShare, share_id});
  }
}

void MediaController::PushVideoI420ForSource(StreamKind kind, unsigned int id, const char* y,
                                             const char* u, const char* v, unsigned int width,
                                             unsigned int height, uint64_t timestamp_ms) {
  // Set timeline base from first media frame
  timeline_clock_.SetBaseFromPts(timestamp_ms);

  std::lock_guard<std::mutex> lock(media_mtx_);
  StreamKey key{kind, id};
  auto it = streams_.find(key);
  if (it != streams_.end() && it->second.pipeline) {
    it->second.pipeline->PushVideoI420(y, u, v, width, height, timestamp_ms);
  }
}

void MediaController::PushAudioPCM(StreamKind kind, const uint8_t* pcm_data, size_t pcm_length,
                                   uint32_t sample_rate, uint32_t channels, uint64_t timestamp_ms) {
  // Set timeline base from first media frame
  timeline_clock_.SetBaseFromPts(timestamp_ms);

  // Push audio only to pipelines of the specified stream kind
  IteratePipeline([kind, pcm_data, pcm_length, sample_rate, channels, timestamp_ms](
                      const StreamKey& key, MediaEncodePipeline& pipeline) {
    if (key.kind == kind) {
      pipeline.PushAudioPcm(pcm_data, pcm_length, sample_rate, channels, timestamp_ms);
    }
  });
}

void MediaController::DispatchAudio(const uint8_t* pcm_data, size_t pcm_length,
                                    uint32_t sample_rate, uint32_t channels, int audio_type,
                                    uint32_t user_id, uint64_t timestamp_ms) {
  auto audio_callback = config_.GetAudioCallback();
  if (audio_callback && pcm_data && pcm_length > 0) {
    audio_callback(pcm_data, pcm_length, sample_rate, channels, audio_type, user_id, timestamp_ms);
  }
}

void MediaController::RequestVideoEncoderIdr() {
  IteratePipeline([](const StreamKey&, MediaEncodePipeline& pipeline) { pipeline.RequestIdr(); });
}

void MediaController::EnsureStream(const StreamKey& key) {
  std::lock_guard<std::mutex> lock(media_mtx_);

  if (streams_.count(key)) return;  // already exists

  StreamState state;
  streams_[key] = std::move(state);

  const std::string suffix = config_.GetStreamSuffix(key);
  Logger::GetInstance().Success("Prepared stream " + suffix);

  // If recording is already active, immediately create renderer, start pipeline, and subscribe
  if (is_recording_.load(std::memory_order_acquire)) {
    auto it = streams_.find(key);
    if (it == streams_.end()) {
      return;
    }

    // Create renderer if not already created
    if (!it->second.renderer) {
      CreateRendererForStream(it->second, key);
    }

    // Start pipeline if not already started
    if (!it->second.pipeline) {
      CreatePipelineForStream(it->second, key);
    }

    // Subscribe renderer if not already subscribed
    SubscribeRendererForStream(it->second, key);
  }
}

void MediaController::DestroyStream(const StreamKey& key) {
  const std::string suffix = config_.GetStreamSuffix(key);

  std::shared_ptr<MediaEncodePipeline> pipeline;
  std::unique_ptr<IZoomSDKRenderer> renderer;
  std::unique_ptr<IZoomSDKRendererDelegate> delegate;

  {
    std::lock_guard<std::mutex> lock(media_mtx_);
    auto it = streams_.find(key);
    if (it == streams_.end()) {
      return;
    }

    pipeline = std::move(it->second.pipeline);
    renderer = std::move(it->second.renderer);
    delegate = std::move(it->second.delegate);
    streams_.erase(it);
  }

  // Stop pipeline outside lock
  if (pipeline) {
    pipeline->Stop();
  }

  // Clean up renderer and delegate
  if (renderer) {
    // Note: Skip unSubscribe() to avoid potential segfaults
    destroyRenderer(renderer.get());
    renderer.release();
  }
  // delegate will be automatically deleted when unique_ptr goes out of scope

  Logger::GetInstance().Success("Destroyed stream " + suffix);
}

void MediaController::CreateRendererForStream(StreamState& state, const StreamKey& key) {
  const std::string suffix = config_.GetStreamSuffix(key);

  if (!video_delegate_factory_) {
    Logger::GetInstance().Warn("No video delegate factory set for " + suffix);
    return;
  }

  auto raw_data_type =
      (key.kind == StreamKind::kCamera) ? RAW_DATA_TYPE_VIDEO : RAW_DATA_TYPE_SHARE;
  auto* stream_delegate = video_delegate_factory_(raw_data_type);
  if (!stream_delegate) {
    Logger::GetInstance().Error("Factory returned null delegate for " + suffix);
    return;
  }

  IZoomSDKRenderer* renderer = nullptr;
  SDKError err = createRenderer(&renderer, stream_delegate);
  if (err != SDKERR_SUCCESS || !renderer) {
    Logger::GetInstance().Error("Failed to create renderer for " + suffix +
                                " (error: " + std::to_string(err) + ")");
    delete stream_delegate;
    return;
  }

  renderer->setRawDataResolution(ZoomSDKResolution_720P);

  // Subscribe immediately if recording is already active, otherwise defer until
  // startMedia()
  if (is_recording_.load(std::memory_order_acquire)) {
    if (renderer->subscribe(key.id, raw_data_type) == SDKERR_SUCCESS) {
      state.renderer.reset(renderer);
      state.delegate.reset(stream_delegate);
      Logger::GetInstance().Success("Subscribed to " + suffix);
    } else {
      Logger::GetInstance().Error("Failed to subscribe to " + suffix);
      destroyRenderer(renderer);
      delete stream_delegate;
    }
  } else {
    // Defer subscription until startMedia()
    state.renderer.reset(renderer);
    state.delegate.reset(stream_delegate);
    Logger::GetInstance().Info("Created renderer for " + suffix + " (subscription deferred)");
  }
}

void MediaController::CreatePipelineForStream(StreamState& state, const StreamKey& key) {
  const std::string suffix = config_.GetStreamSuffix(key);

  auto video_encoder_cfg = config_.GetVideoEncoderConfigForStream(key);
  auto audio_encoder_cfg = config_.GetAudioEncoderConfig();
  auto muxer_cfg = config_.GetMuxerConfigForStream(key);
  auto hls_file_callback = config_.GetHlsFileCallback();

  if (!video_encoder_cfg || !audio_encoder_cfg || !muxer_cfg || !hls_file_callback) {
    Logger::GetInstance().Warn("Configs not set; cannot start pipeline for " + suffix);
    return;
  }

  auto pipeline = std::make_shared<MediaEncodePipeline>();
  if (pipeline->Start(*video_encoder_cfg, *audio_encoder_cfg, *muxer_cfg, hls_file_callback)) {
    state.pipeline = pipeline;
    Logger::GetInstance().Success("Started pipeline for " + suffix);
  } else {
    Logger::GetInstance().Error("Failed to start pipeline for " + suffix);
  }
}

void MediaController::SubscribeRendererForStream(StreamState& state, const StreamKey& key) {
  const std::string suffix = config_.GetStreamSuffix(key);

  if (!state.renderer || !state.delegate) {
    return;
  }

  if (state.renderer->getSubscribeId() != 0) {
    return;  // Already subscribed
  }

  auto raw_type = (key.kind == StreamKind::kCamera) ? RAW_DATA_TYPE_VIDEO : RAW_DATA_TYPE_SHARE;
  std::string type_str = (raw_type == RAW_DATA_TYPE_VIDEO) ? "VIDEO" : "SHARE";
  Logger::GetInstance().Info("Subscribing " + suffix + " with ID=" + std::to_string(key.id) +
                             " type=" + type_str);

  SDKError sub_err = state.renderer->subscribe(key.id, raw_type);
  if (sub_err == SDKERR_SUCCESS) {
    Logger::GetInstance().Success("Subscribed to " + suffix + " (subscribeId=" +
                                  std::to_string(state.renderer->getSubscribeId()) + ")");
  } else {
    Logger::GetInstance().Error("Failed to subscribe to " + suffix +
                                " (error=" + std::to_string(sub_err) + ")");
  }
}

void MediaController::IteratePipeline(
    const std::function<void(const StreamKey&, MediaEncodePipeline&)>& fn) {
  IterateStream([&fn](const StreamKey& key, StreamState& state) {
    if (state.pipeline) {
      fn(key, *state.pipeline);
    }
  });
}

void MediaController::UpdateCameraStatus(unsigned int user_id, bool video_on) {
  if (video_on) {
    EnsureStream({StreamKind::kCamera, user_id});
  } else {
    DestroyStream({StreamKind::kCamera, user_id});
  }
}

void MediaController::SetupRecording(IMeetingRecordingController* ctrl, bool use_raw_audio,
                                     bool use_raw_video) {
  if (!ctrl) {
    Logger::GetInstance().Error("Recording controller is null");
    return;
  }

  recording_controller_ = ctrl;
  use_raw_audio_ = use_raw_audio;
  use_raw_video_ = use_raw_video;

  // Create recording event with callback to handle privilege changes
  std::function<void(bool)> on_recording_privilege_changed = [this](bool can_rec) {
    OnRecordingPrivilegeChanged(can_rec);
  };

  recording_event_ = std::make_unique<MeetingRecordingCtrlEvent>(on_recording_privilege_changed);
  recording_controller_->SetEvent(recording_event_.get());

  // Check if we can start recording immediately
  auto err = recording_controller_->CanStartRawRecording();
  if (err == SDKERR_SUCCESS) {
    OnRecordingPrivilegeChanged(true);
  } else {
    recording_controller_->RequestLocalRecordingPrivilege();
  }
}

void MediaController::CleanupRecording() {
  if (recording_controller_ && recording_event_) {
    recording_controller_->SetEvent(nullptr);
  }
  recording_event_.reset();
  recording_controller_ = nullptr;
}

void MediaController::OnRecordingPrivilegeChanged(bool can_record) {
  if (can_record) {
    auto err = StartRawRecording();
    if (err == SDKERR_SUCCESS) {
      StartMedia(use_raw_audio_, use_raw_video_, {});
    }
  } else {
    StopMedia();
    StopRawRecording();
  }
}

SDKError MediaController::StartRawRecording() {
  if (!recording_controller_) {
    Logger::GetInstance().Error("Recording controller not set");
    return SDKERR_INVALID_PARAMETER;
  }

  // Always refresh media pipelines/subscriptions
  if (!is_recording_.load(std::memory_order_acquire)) {
    ZOOM_ERR_CHECK(recording_controller_->StartRawRecording(), "start raw recording");
  }

  return SDKERR_SUCCESS;
}

SDKError MediaController::StopRawRecording() {
  if (!recording_controller_) {
    return SDKERR_SUCCESS;
  }

  if (!is_recording_.load(std::memory_order_acquire)) {
    return SDKERR_SUCCESS;
  }

  return recording_controller_->StopRawRecording();
}
