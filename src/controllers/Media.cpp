#include "controllers/Media.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <set>
#include <unordered_map>

#include "util/Checks.h"
#include "util/Logger.h"

using namespace ZOOMSDK;

MediaController::MediaController()
    : is_recording_(false), use_raw_audio_(false), use_raw_video_(false) {
  // Initialize controllers
  video_controller_ = std::make_unique<VideoController>(video_config_, timeline_clock_);
  audio_controller_ = std::make_unique<AudioController>(audio_config_, timeline_clock_);
}

MediaController::~MediaController() noexcept {
  CleanupRecording();
  StopMedia();
}

void MediaController::SetVideoDelegateFactory(VideoDelegateFactory factory) {
  video_controller_->SetVideoDelegateFactory(std::move(factory));
}

void MediaController::SetAudioDelegateFactory(AudioDelegateFactory factory) {
  audio_controller_->SetAudioDelegateFactory(std::move(factory));
}

SDKError MediaController::StartMedia(bool use_raw_audio, bool use_raw_video,
                                     const std::vector<ZoomSDKSharingSourceInfo>& initial_shares) {
  const bool was_recording = is_recording_.load(std::memory_order_acquire);

  if (use_raw_video && !initial_shares.empty()) {
    video_controller_->UpdateShareSources(initial_shares);
  }

  is_recording_.store(true, std::memory_order_release);

  if (use_raw_audio) {
    audio_controller_->StartRecording();
  }

  if (use_raw_video) {
    video_controller_->StartRecording();
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

  video_controller_->StopRecording();
  audio_controller_->StopRecording();

  Logger::GetInstance().Success("Raw recording stopped");
  timeline_clock_.Reset();
  return SDKERR_SUCCESS;
}

void MediaController::UpdateShareSources(const std::vector<ZoomSDKSharingSourceInfo>& sources) {
  video_controller_->UpdateShareSources(sources);
}

void MediaController::PushVideoI420(StreamKind kind, unsigned int id, const char* y, const char* u,
                                    const char* v, unsigned int width, unsigned int height,
                                    uint64_t timestamp_ms) {
  timeline_clock_.SetBaseFromPts(timestamp_ms);
  video_controller_->PushVideoFrame(kind, id, y, u, v, width, height, timestamp_ms);
}

void MediaController::PushAudioToHlsPipelines(StreamKind kind, const uint8_t* pcm_data,
                                              size_t pcm_length, uint32_t sample_rate,
                                              uint32_t channels, uint64_t timestamp_ms) {
  timeline_clock_.SetBaseFromPts(timestamp_ms);
  video_controller_->PushAudioForHls(kind, pcm_data, pcm_length, sample_rate, channels,
                                     timestamp_ms);
}

void MediaController::PushAudioEncoded(const uint8_t* pcm_data, size_t pcm_length,
                                       uint32_t sample_rate, uint32_t channels, int audio_type,
                                       uint32_t user_id, uint64_t timestamp_ms) {
  audio_controller_->PushAudioFrame(pcm_data, pcm_length, sample_rate, channels, audio_type,
                                    user_id, timestamp_ms);
}

void MediaController::UpdateCameraStatus(unsigned int user_id, bool video_on) {
  video_controller_->UpdateCameraStatus(user_id, video_on);
}

void MediaController::UpdateAudioStatus(unsigned int user_id, ZOOMSDK::AudioStatus status) {
  audio_controller_->UpdateAudioStatus(user_id, status);
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

  std::function<void(bool)> on_recording_privilege_changed = [this](bool can_rec) {
    OnRecordingPrivilegeChanged(can_rec);
  };

  recording_event_ = std::make_unique<MeetingRecordingCtrlEvent>(on_recording_privilege_changed);
  recording_controller_->SetEvent(recording_event_.get());

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
