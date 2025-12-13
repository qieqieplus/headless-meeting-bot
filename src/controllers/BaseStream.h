#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "util/Logger.h"

// Base Stream Controller using CRTP (Curiously Recurring Template Pattern)
// This template provides common stream lifecycle management
template <typename Key, typename State, typename KeyHash, typename Derived>
class BaseStreamController {
 public:
  BaseStreamController() = default;
  virtual ~BaseStreamController() = default;

  // Non-copyable, non-movable
  BaseStreamController(const BaseStreamController&) = delete;
  BaseStreamController& operator=(const BaseStreamController&) = delete;

 protected:
  // Check if a stream exists
  bool HasStream(const Key& key) const {
    std::lock_guard<std::mutex> lock(streams_mtx_);
    return streams_.count(key) > 0;
  }

  // Ensure a stream exists (create if needed)
  // Returns true if stream was newly created
  bool EnsureStreamExists(const Key& key) {
    std::lock_guard<std::mutex> lock(streams_mtx_);
    if (streams_.count(key)) {
      return false;
    }
    State state{};
    streams_[key] = std::move(state);
    return true;
  }

  // Remove a stream
  // Returns the state if it existed (for cleanup outside the lock)
  bool RemoveStream(const Key& key, State& out_state) {
    std::lock_guard<std::mutex> lock(streams_mtx_);
    auto it = streams_.find(key);
    if (it == streams_.end()) {
      return false;
    }
    out_state = std::move(it->second);
    streams_.erase(it);
    return true;
  }

  // Get a copy of a stream state (thread-safe)
  bool GetStreamState(const Key& key, State& out_state) const {
    std::lock_guard<std::mutex> lock(streams_mtx_);
    auto it = streams_.find(key);
    if (it == streams_.end()) {
      return false;
    }
    out_state = it->second;
    return true;
  }

  // Update a stream state (thread-safe)
  template <typename Func>
  bool UpdateStreamState(const Key& key, Func&& updater) {
    std::lock_guard<std::mutex> lock(streams_mtx_);
    auto it = streams_.find(key);
    if (it == streams_.end()) {
      return false;
    }
    updater(it->second);
    return true;
  }

  // Access a stream state (thread-safe, const version)
  template <typename Func>
  bool AccessStreamState(const Key& key, Func&& accessor) const {
    std::lock_guard<std::mutex> lock(streams_mtx_);
    auto it = streams_.find(key);
    if (it == streams_.end()) {
      return false;
    }
    accessor(it->second);
    return true;
  }

  // Execute a function on all streams
  template <typename Func>
  void ForEachStream(Func&& func) {
    std::lock_guard<std::mutex> lock(streams_mtx_);
    for (auto& [key, state] : streams_) {
      func(key, state);
    }
  }

  // Get all stream keys
  std::vector<Key> GetAllKeys() const {
    std::lock_guard<std::mutex> lock(streams_mtx_);
    std::vector<Key> keys;
    keys.reserve(streams_.size());
    for (const auto& [key, _] : streams_) {
      keys.push_back(key);
    }
    return keys;
  }

  // Clear all streams
  // Returns all states for cleanup outside the lock
  std::unordered_map<Key, State, KeyHash> ClearAllStreams() {
    std::lock_guard<std::mutex> lock(streams_mtx_);
    auto streams = std::move(streams_);
    streams_.clear();
    return streams;
  }

  // Access to the mutex for complex operations
  std::mutex& GetStreamsMutex() const { return streams_mtx_; }

 private:
  mutable std::mutex streams_mtx_;
  std::unordered_map<Key, State, KeyHash> streams_;
};

// Helper function for logging stream events with different levels
inline void LogStreamEvent(const std::string& message, const std::string& level = "success") {
  static const std::unordered_map<std::string, std::function<void(const std::string&)>> loggers = {
      {"success", [](const std::string& m) { Logger::GetInstance().Success(m); }},
      {"error", [](const std::string& m) { Logger::GetInstance().Error(m); }},
      {"warn", [](const std::string& m) { Logger::GetInstance().Warn(m); }}};
  auto it = loggers.find(level);
  if (it != loggers.end()) {
    it->second(message);
  } else {
    Logger::GetInstance().Info(message);
  }
}
