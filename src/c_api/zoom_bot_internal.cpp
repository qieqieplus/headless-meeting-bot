#include "zoom_bot_internal.h"

#include <mutex>
#include <unordered_set>

#include "Meeting.h"
#include "ZoomSDK.h"

// Internal global state for C layer
namespace {
template <typename T>
class HandleRegistry {
 public:
  using Pointer = T*;

  Pointer Register(Pointer ptr) {
    if (!ptr) return nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    instances_.insert(ptr);
    return ptr;
  }

  void Unregister(Pointer ptr) {
    if (!ptr) return;
    std::lock_guard<std::mutex> lock(mutex_);
    instances_.erase(ptr);
  }

  Pointer Resolve(Pointer ptr) const {
    if (!ptr) return nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    return instances_.count(ptr) ? ptr : nullptr;
  }

 private:
  mutable std::mutex mutex_;
  std::unordered_set<Pointer> instances_;
};

HandleRegistry<ZoomSDK> g_sdk_registry;
HandleRegistry<Meeting> g_meeting_registry;

}  // namespace

namespace Impl {

// Internal handle/state APIs
ZoomBotHandle CreateSDKHandle(ZoomSDK* sdk_ptr) {
  auto* stored = g_sdk_registry.Register(sdk_ptr);
  return reinterpret_cast<ZoomBotHandle>(stored);
}

void DestroySDKHandle(ZoomBotHandle sdk_handle) {
  g_sdk_registry.Unregister(reinterpret_cast<ZoomSDK*>(sdk_handle));
}

ZoomSDK* GetSDKFromHandle(ZoomBotHandle sdk_handle) {
  return g_sdk_registry.Resolve(reinterpret_cast<ZoomSDK*>(sdk_handle));
}

MeetingHandle CreateMeetingHandle(Meeting* meeting_ptr) {
  auto* stored = g_meeting_registry.Register(meeting_ptr);
  return reinterpret_cast<MeetingHandle>(stored);
}

void DestroyMeetingHandle(MeetingHandle meeting_handle) {
  g_meeting_registry.Unregister(reinterpret_cast<Meeting*>(meeting_handle));
}

Meeting* GetMeetingFromHandle(MeetingHandle handle) {
  return g_meeting_registry.Resolve(reinterpret_cast<Meeting*>(handle));
}

}  // namespace Impl
