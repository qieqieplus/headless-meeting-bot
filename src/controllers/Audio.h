#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include "controllers/AudioConfig.h"
#include "controllers/BaseStream.h"
#include "meeting_service_components/meeting_audio_interface.h"
#include "rawdata/rawdata_audio_helper_interface.h"
#include "util/TimelineClock.h"
#include "zoom_sdk_def.h"

// Forward declarations
class AudioEncodePipeline;
struct AudioEncoderConfig;

class AudioController : public BaseStreamController<AudioStreamKey, struct AudioStreamState,
                                                    AudioStreamKeyHash, AudioController> {
 public:
  using AudioCallback = AudioConfig::AudioCallback;
  using AudioDelegateFactory = std::function<ZOOMSDK::IZoomSDKAudioRawDataDelegate*()>;

  explicit AudioController(AudioConfig& config, TimelineClock& timeline_clock);
  ~AudioController() noexcept;

  // === Configuration ===
  void SetAudioDelegateFactory(AudioDelegateFactory factory) {
    audio_delegate_factory_ = std::move(factory);
  }

  // === Stream Management ===
  void UpdateAudioStatus(unsigned int user_id, ZOOMSDK::AudioStatus status);

  // === Data Push ===
  // Push audio PCM to standalone audio encoding pipelines
  void PushAudioFrame(const uint8_t* pcm_data, size_t pcm_length, uint32_t sample_rate,
                      uint32_t channels, int audio_type, uint32_t user_id, uint64_t timestamp_ms);

  // === Recording Lifecycle ===
  void StartRecording();
  void StopRecording();

 private:
  struct AudioHelperDeleter {
    void operator()(ZOOMSDK::IZoomSDKAudioRawDataHelper* helper) const {
      if (helper) {
        helper->unSubscribe();
      }
    }
  };

  friend struct AudioStreamState;

  // Helper functions
  void LogStreamEvent(const AudioStreamKey& key, const std::string& event,
                      const std::string& level = "success");
  bool ActivateAudioComponents(AudioStreamState& state, const AudioStreamKey& key);
  bool CreateAudioHelper(AudioStreamState& state, const AudioStreamKey& key);
  bool CreateAudioPipeline(AudioStreamState& state, const AudioStreamKey& key);
  void SubscribeAudioHelper(AudioStreamState& state, const AudioStreamKey& key);

  void EnsureAudioStream(int audio_type, uint32_t user_id);
  void DestroyAudioStream(int audio_type, uint32_t user_id);
  void ActivateAudioStreams();
  void SetRawPcmFilename(AudioStreamState& state, int audio_type, uint32_t user_id);
  uint64_t GetCurrentTimestampMs() const;

  AudioConfig& config_;
  TimelineClock& timeline_clock_;
  AudioDelegateFactory audio_delegate_factory_;
  std::atomic<bool> is_recording_;

  // Global audio helper (shared for all streams, similar to how Zoom SDK works)
  std::unique_ptr<ZOOMSDK::IZoomSDKAudioRawDataHelper, AudioHelperDeleter> audio_helper_;
  ZOOMSDK::IZoomSDKAudioRawDataDelegate* audio_delegate_;  // Owned by SDK after subscription
};

struct AudioStreamState {
  std::shared_ptr<class AudioEncodePipeline> pipeline;
  std::string filename;  // Output filename/path
  bool is_subscribed;    // Whether this stream is subscribed to the audio helper
};
