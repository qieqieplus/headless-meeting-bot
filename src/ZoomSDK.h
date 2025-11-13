#ifndef HEADLESS_ZOOM_BOT_ZOOM_SDK_H
#define HEADLESS_ZOOM_BOT_ZOOM_SDK_H

#include <jwt-cpp/jwt.h>

#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "SDKConfig.h"
#include "auth_service_interface.h"
#include "events/AuthServiceEvent.h"
#include "meeting_service_interface.h"
#include "network_connection_handler_interface.h"
#include "setting_service_interface.h"
#include "zoom_sdk.h"

typedef std::chrono::time_point<std::chrono::system_clock> TimePoint;

// Forward declarations
class Meeting;
class MeetingConfig;

class ZoomSDK {
  std::string jwt_;
  TimePoint iat_;
  TimePoint exp_;

  ZOOMSDK::IAuthService* auth_service_;
  ZOOMSDK::ISettingService* setting_service_;
  ZOOMSDK::INetworkConnectionHelper* network_helper_;
  ZOOMSDK::IMeetingService* meeting_service_;
  std::unique_ptr<AuthServiceEvent> auth_event_;

  std::string sdk_key_;
  std::string sdk_secret_;
  std::string zoom_host_;

  bool is_initialized_;
  bool is_authenticated_;

  std::function<void()> on_auth_callback_;

  ZOOMSDK::SDKError CreateGlobalServices();
  void GenerateJwt(const std::string& key, const std::string& secret);

 public:
  ZoomSDK();
  ~ZoomSDK() noexcept;

  // Disable copy and move operations - resource management is complex
  ZoomSDK(const ZoomSDK&) = delete;
  ZoomSDK& operator=(const ZoomSDK&) = delete;
  ZoomSDK(ZoomSDK&&) = delete;
  ZoomSDK& operator=(ZoomSDK&&) = delete;

  ZOOMSDK::SDKError Initialize(const SDKConfig& config);
  ZOOMSDK::SDKError Initialize(const std::string& sdk_key, const std::string& sdk_secret,
                               const std::string& zoom_host = "https://zoom.com");
  ZOOMSDK::SDKError Authenticate(std::function<void()> on_auth_callback = nullptr);

  ZOOMSDK::SDKError Cleanup() noexcept;

  bool IsInitialized() const noexcept { return is_initialized_; }
  bool IsAuthenticated() const noexcept { return is_authenticated_; }

  // Access to global services
  ZOOMSDK::ISettingService* GetSettingService() const noexcept { return setting_service_; }
  ZOOMSDK::INetworkConnectionHelper* GetNetworkHelper() const noexcept { return network_helper_; }
  ZOOMSDK::IMeetingService* GetMeetingService() const noexcept { return meeting_service_; }
};

#endif  // HEADLESS_ZOOM_BOT_ZOOM_SDK_H
