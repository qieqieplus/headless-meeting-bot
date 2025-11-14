#pragma once

#include <string>

class SDKConfig {
 private:
  std::string sdk_key_;
  std::string sdk_secret_;
  std::string zoom_host_;

 public:
  explicit SDKConfig(const std::string& sdk_key = "", const std::string& sdk_secret = "",
                     const std::string& zoom_host = "https://zoom.us");

  // Getters
  const std::string& SdkKey() const { return sdk_key_; }
  const std::string& SdkSecret() const { return sdk_secret_; }
  const std::string& ZoomHost() const { return zoom_host_; }

  // Setters
  void SetSdkKey(const std::string& sdk_key) { sdk_key_ = sdk_key; }
  void SetSdkSecret(const std::string& sdk_secret) { sdk_secret_ = sdk_secret; }
  void SetZoomHost(const std::string& zoom_host) { zoom_host_ = zoom_host; }

  // Validation
  bool IsValid() const { return !sdk_key_.empty() && !sdk_secret_.empty(); }
};
