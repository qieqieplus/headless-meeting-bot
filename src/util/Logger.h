#ifndef HEADLESS_ZOOM_BOT_LOGGER_H
#define HEADLESS_ZOOM_BOT_LOGGER_H

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <mutex>
#include <memory>
#include <chrono>
#include <iomanip>

namespace Util {

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
    SUCCESS = 4,
    QUIET = 5
};

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    // Prevent copying
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // Configuration methods
    void setLogLevel(LogLevel level) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_currentLevel = level;
    }

    void setLogFile(const std::string& filename) {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Close existing file stream
        if (m_fileStream && m_fileStream->is_open()) {
            m_fileStream->close();
        }

        m_logFile = filename;
        if (!filename.empty()) {
            m_fileStream = std::make_unique<std::ofstream>(filename, std::ios::app);
        }
    }

    void enableConsoleOutput(bool enable) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_consoleOutput = enable;
    }

    void enableFileOutput(bool enable) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_fileOutput = enable;
    }

    // Logging methods
    void log(LogLevel level, const std::string& message) {
        if (level < m_currentLevel) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        std::string formattedMessage = formatMessage(level, message);

        if (m_consoleOutput) {
            if (level == LogLevel::ERROR) {
                std::cerr << formattedMessage << std::endl;
            } else {
                std::cout << formattedMessage << std::endl;
            }
        }

        if (m_fileOutput && m_fileStream && m_fileStream->is_open()) {
            *m_fileStream << formattedMessage << std::endl;
        }
    }

    void debug(const std::string& message) {
        log(LogLevel::DEBUG, message);
    }

    void info(const std::string& message) {
        log(LogLevel::INFO, message);
    }

    void warn(const std::string& message) {
        log(LogLevel::WARN, message);
    }

    void error(const std::string& message) {
        log(LogLevel::ERROR, message);
    }

    void success(const std::string& message) {
        log(LogLevel::SUCCESS, message);
    }

    // Utility methods
    bool hasError(const std::string& action, bool condition, const std::string& errorMessage = "") {
        if (condition) {
            std::stringstream ss;
            ss << "failed to " << action;
            if (!errorMessage.empty()) {
                ss << ": " << errorMessage;
            }
            error(ss.str());
        } else {
            success(action);
        }
        return condition;
    }

private:
    Logger() : m_currentLevel(LogLevel::INFO), m_consoleOutput(true), m_fileOutput(false), m_logFile("") {}

    std::string getTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S")
           << "." << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    std::string formatMessage(LogLevel level, const std::string& message) {
        std::string icon;
        switch (level) {
            case LogLevel::DEBUG:
                icon = "🔍";
                break;
            case LogLevel::INFO:
                icon = "⏳";
                break;
            case LogLevel::WARN:
                icon = "⚠️";
                break;
            case LogLevel::ERROR:
                icon = "❌";
                break;
            case LogLevel::SUCCESS:
                icon = "✅";
                break;
            default:
                icon = "";
                break;
        }

        std::stringstream ss;
        ss << "[" << getTimestamp() << "] " << icon << " " << message;
        return ss.str();
    }

    LogLevel m_currentLevel;
    bool m_consoleOutput;
    bool m_fileOutput;
    std::string m_logFile;
    std::unique_ptr<std::ofstream> m_fileStream;
    std::mutex m_mutex;
};

} // namespace Logger

#endif // HEADLESS_ZOOM_BOT_LOGGER_H
