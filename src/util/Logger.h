#pragma once

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

enum class LogLevel { kDebug = 0, kInfo = 1, kWarn = 2, kError = 3, kSuccess = 4, kQuiet = 5 };

class Logger {
 public:
  static Logger& GetInstance() {
    static Logger instance;
    return instance;
  }

  // Prevent copying
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  // Configuration methods
  void SetLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_level_ = level;
  }

  void SetLogFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Close existing file stream
    if (file_stream_ && file_stream_->is_open()) {
      file_stream_->close();
    }

    log_file_ = filename;
    if (!filename.empty()) {
      file_stream_ = std::make_unique<std::ofstream>(filename, std::ios::app);
    }
  }

  void EnableConsoleOutput(bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    console_output_ = enable;
  }

  void EnableFileOutput(bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    file_output_ = enable;
  }

  // Logging methods
  void Log(LogLevel level, const std::string& message) {
    if (level < current_level_) {
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::string formatted_message = FormatMessage(level, message);

    if (console_output_) {
      if (level == LogLevel::kError) {
        std::cerr << formatted_message << std::endl;
      } else {
        std::cout << formatted_message << std::endl;
      }
    }

    if (file_output_ && file_stream_ && file_stream_->is_open()) {
      *file_stream_ << formatted_message << std::endl;
    }
  }

  void Debug(const std::string& message) { Log(LogLevel::kDebug, message); }

  void Info(const std::string& message) { Log(LogLevel::kInfo, message); }

  void Warn(const std::string& message) { Log(LogLevel::kWarn, message); }

  void Error(const std::string& message) { Log(LogLevel::kError, message); }

  void Success(const std::string& message) { Log(LogLevel::kSuccess, message); }

  // Utility methods
  bool HasError(const std::string& action, bool condition, const std::string& error_message = "") {
    if (condition) {
      std::stringstream ss;
      ss << "failed to " << action;
      if (!error_message.empty()) {
        ss << ": " << error_message;
      }
      Error(ss.str());
    } else {
      Success(action);
    }
    return condition;
  }

 private:
  Logger()
      : current_level_(LogLevel::kInfo),
        console_output_(true),
        file_output_(false),
        log_file_("") {}

  std::string GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm local_time{};
    localtime_r(&time, &local_time);

    std::stringstream ss;
    ss << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S") << "." << std::setfill('0')
       << std::setw(3) << ms.count();
    return ss.str();
  }

  std::string FormatMessage(LogLevel level, const std::string& message) {
    std::string icon;
    switch (level) {
      case LogLevel::kDebug:
        icon = "🔍";
        break;
      case LogLevel::kInfo:
        icon = "⏳";
        break;
      case LogLevel::kWarn:
        icon = "⚠️";
        break;
      case LogLevel::kError:
        icon = "❌";
        break;
      case LogLevel::kSuccess:
        icon = "✅";
        break;
      default:
        icon = "";
        break;
    }

    std::stringstream ss;
    ss << "[" << GetTimestamp() << "] " << icon << " " << message;
    return ss.str();
  }

  LogLevel current_level_;
  bool console_output_;
  bool file_output_;
  std::string log_file_;
  std::unique_ptr<std::ofstream> file_stream_;
  std::mutex mutex_;
};
