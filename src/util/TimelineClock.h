#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>

class TimelineClock {
 public:
  TimelineClock() : base_set_(false), media_pts_base_(0), wall_clock_base_(0) {}

  inline void SetBaseFromPts(uint64_t media_pts_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (base_set_) return;

    media_pts_base_ = media_pts_ms;
    wall_clock_base_ = WallClockMs();
    base_set_ = true;
  }

  inline uint64_t NowToMediaMs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!base_set_) return 0;

    uint64_t now_wall = WallClockMs();

    uint64_t elapsed_wall = now_wall - wall_clock_base_;
    return media_pts_base_ + elapsed_wall;
  }

  static inline uint64_t WallClockMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  inline bool IsBaseSet() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return base_set_;
  }

  inline void Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    base_set_ = false;
    media_pts_base_ = 0;
    wall_clock_base_ = 0;
  }

 private:
  bool base_set_;
  uint64_t media_pts_base_;
  uint64_t wall_clock_base_;
  mutable std::mutex mutex_;
};
