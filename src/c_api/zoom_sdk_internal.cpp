#include "zoom_sdk_internal.h"
#include "MediaController.h"
#include "Meeting.h"
#include "util/Checks.h"

#include <mutex>
#include <unordered_set>

// Internal global state for C layer
namespace {
std::mutex g_instance_mutex;
std::unordered_set<void *> g_sdk_instances;
std::unordered_set<MeetingHandle> g_meeting_instances;
} // namespace

// Template helper function
template <typename T, typename HandleType>
static T *
get_from_handle(HandleType handle,
                const std::unordered_set<HandleType> &instances) noexcept {
  if (!handle)
    return nullptr;
  std::lock_guard<std::mutex> lock(g_instance_mutex);
  return instances.count(handle) ? reinterpret_cast<T *>(handle) : nullptr;
}

namespace Impl {
Meeting *getMeetingFromHandle(MeetingHandle handle) {
  return get_from_handle<Meeting, MeetingHandle>(handle, g_meeting_instances);
}

// Internal handle/state APIs
ZoomSDKHandle createSdkHandle(ZoomSDKHandle sdk_ptr) {
  std::lock_guard<std::mutex> lock(g_instance_mutex);
  g_sdk_instances.insert(sdk_ptr);
  return sdk_ptr;
}

void destroySdkHandle(ZoomSDKHandle sdk_handle) {
  std::lock_guard<std::mutex> lock(g_instance_mutex);
  g_sdk_instances.erase(sdk_handle);
}

ZoomSDKHandle getSdkFromHandle(ZoomSDKHandle sdk_handle) {
  std::lock_guard<std::mutex> lock(g_instance_mutex);
  return g_sdk_instances.count(sdk_handle) ? sdk_handle : nullptr;
}

MeetingHandle createMeetingHandle(MeetingHandle meeting_ptr) {
  std::lock_guard<std::mutex> lock(g_instance_mutex);
  g_meeting_instances.insert(meeting_ptr);
  return meeting_ptr;
}

void destroyMeetingHandle(MeetingHandle meeting_handle) {
  std::lock_guard<std::mutex> lock(g_instance_mutex);
  g_meeting_instances.erase(meeting_handle);
}

/**
 * Common helper to get MediaController from meeting handle with validation
 */
static MediaController *get_media_controller(MeetingHandle meeting_handle) {
  if (!meeting_handle)
    return nullptr;
  Meeting *meeting = getMeetingFromHandle(meeting_handle);
  return meeting ? meeting->getMediaController() : nullptr;
}

void zoom_meeting_dispatch_video(MeetingHandle meeting_handle,
                                 const char *y_buffer, const char *u_buffer,
                                 const char *v_buffer, unsigned int width,
                                 unsigned int height, unsigned int buffer_len,
                                 unsigned int source_id,
                                 unsigned long long timestamp) {
  if (!y_buffer || !u_buffer || !v_buffer || buffer_len == 0) {
    return;
  }
  Meeting *meeting = getMeetingFromHandle(meeting_handle);
  ASSERT_NOT_NULL(meeting);
  auto *mediaCtrl = meeting->getMediaController();
  ASSERT_NOT_NULL(mediaCtrl);
  mediaCtrl->pushVideoI420(y_buffer, u_buffer, v_buffer, width, height,
                           timestamp);
}

/**
 * Dispatch mixed audio data to MediaController for both raw callback and HLS
 * pipeline Called internally by ZoomSDKAudioRawDataDelegate
 */
void zoom_meeting_dispatch_mixed_audio(MeetingHandle meeting_handle,
                                       const void *pcm_data,
                                       unsigned int length,
                                       unsigned int sample_rate,
                                       unsigned int channels,
                                       unsigned long long timestamp) {
  if (!pcm_data || length == 0)
    return;

  MediaController *mediaCtrl = get_media_controller(meeting_handle);
  ASSERT_NOT_NULL(mediaCtrl);

  const uint8_t *pcmData = static_cast<const uint8_t *>(pcm_data);

  // Dispatch raw audio to user callback + push to HLS encoding pipeline
  mediaCtrl->dispatchAudio(pcmData, length, sample_rate, channels,
                           ZOOM_AUDIO_TYPE_MIXED, 0, timestamp);
  mediaCtrl->pushAudioPCM(pcmData, length, sample_rate, channels, timestamp);
}

/**
 * Dispatch one-way audio data to MediaController for raw callback only
 * Called internally by ZoomSDKAudioRawDataDelegate
 */
void zoom_meeting_dispatch_one_way_audio(
    MeetingHandle meeting_handle, const void *pcm_data, unsigned int length,
    unsigned int sample_rate, unsigned int channels, unsigned int user_id,
    unsigned long long timestamp) {
  if (!pcm_data || length == 0)
    return;

  MediaController *mediaCtrl = get_media_controller(meeting_handle);
  ASSERT_NOT_NULL(mediaCtrl);

  const uint8_t *pcmData = static_cast<const uint8_t *>(pcm_data);

  // Dispatch raw audio to user callback only (no HLS for one-way audio)
  mediaCtrl->dispatchAudio(pcmData, length, sample_rate, channels,
                           ZOOM_AUDIO_TYPE_ONE_WAY, user_id, timestamp);
}

} // namespace Impl
