#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>

/// \brief Simple timeline clock anchored to first media frame (mixed audio).
///
/// Establishes a single timeline base (t0) from the first media frame received.
/// All events are measured relative to this point: mediaTs = unixMs - t0.
///
/// Events occurring before recording starts will have negative mediaTs values.
/// This ensures perfect sync between audio, video, and events during playback.
class TimelineClock {
 public:
  TimelineClock() : t0_unix_ms_(std::nullopt) {}

  /// \brief Set timeline base from first media frame (typically mixed audio).
  /// Only the first call takes effect; subsequent calls are ignored.
  inline void SetBase() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (t0_unix_ms_) return;  // Already set
    t0_unix_ms_ = SystemUnixMs();
  }

  /// \brief Set timeline base from a specific timestamp (e.g. from media frame).
  /// Only the first call takes effect; subsequent calls are ignored.
  inline void SetBaseFromPts(uint64_t pts) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (t0_unix_ms_) return;  // Already set
    t0_unix_ms_ = pts;
  }

  /// \brief Convert unix timestamp to media timeline (can be negative).
  /// \param unix_ms Absolute unix epoch time in milliseconds
  /// \return Media timeline timestamp in ms (negative if before t0, 0 if not initialized)
  inline int64_t UnixToMediaMs(uint64_t unix_ms) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!t0_unix_ms_) return 0;
    return static_cast<int64_t>(unix_ms) - static_cast<int64_t>(*t0_unix_ms_);
  }

  /// \brief Convert current system time to media timeline.
  /// \return Current media timeline timestamp in milliseconds (0 if not initialized)
  inline int64_t NowToMediaMs() const { return UnixToMediaMs(SystemUnixMs()); }

  /// \brief Get the unix epoch time when timeline was established (t0).
  /// \return Unix milliseconds at t0, or nullopt if not set
  inline std::optional<uint64_t> GetT0UnixMs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return t0_unix_ms_;
  }

  /// \brief Check if timeline base has been set.
  inline bool IsBaseSet() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return t0_unix_ms_.has_value();
  }

  /// \brief Reset timeline (for reuse).
  inline void Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    t0_unix_ms_ = std::nullopt;
  }

  /// \brief Get current system time in milliseconds (unix epoch).
  static inline uint64_t SystemUnixMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  }

 private:
  std::optional<uint64_t> t0_unix_ms_;  // Unix epoch time when first media frame arrived
  mutable std::mutex mutex_;
};
