#ifndef HEADLESS_ZOOM_BOT_SDK_CONFIG_H
#define HEADLESS_ZOOM_BOT_SDK_CONFIG_H

#include <string>


class SDKConfig {
private:
    std::string m_sdkKey;
    std::string m_sdkSecret;
    std::string m_zoomHost;

public:
    SDKConfig(const std::string& sdkKey = "",
              const std::string& sdkSecret = "",
              const std::string& zoomHost = "https://zoom.us");

    // Getters
    const std::string& sdkKey() const { return m_sdkKey; }
    const std::string& sdkSecret() const { return m_sdkSecret; }
    const std::string& zoomHost() const { return m_zoomHost; }
    
    // Setters
    void setSdkKey(const std::string& sdkKey) { m_sdkKey = sdkKey; }
    void setSdkSecret(const std::string& sdkSecret) { m_sdkSecret = sdkSecret; }
    void setZoomHost(const std::string& zoomHost) { m_zoomHost = zoomHost; }
    
    // Validation
    bool isValid() const {
        return !m_sdkKey.empty() && !m_sdkSecret.empty();
    }
};

#endif //HEADLESS_ZOOM_BOT_SDK_CONFIG_H
