#ifndef HEADLESS_ZOOM_BOT_SDK_CONFIG_H
#define HEADLESS_ZOOM_BOT_SDK_CONFIG_H

#include <string>

using namespace std;

class SDKConfig {
private:
    string m_sdkKey;
    string m_sdkSecret;
    string m_zoomHost;

public:
    SDKConfig(const string& sdkKey = "",
              const string& sdkSecret = "",
              const string& zoomHost = "https://zoom.us");

    // Getters
    const string& sdkKey() const { return m_sdkKey; }
    const string& sdkSecret() const { return m_sdkSecret; }
    const string& zoomHost() const { return m_zoomHost; }
    
    // Setters
    void setSdkKey(const string& sdkKey) { m_sdkKey = sdkKey; }
    void setSdkSecret(const string& sdkSecret) { m_sdkSecret = sdkSecret; }
    void setZoomHost(const string& zoomHost) { m_zoomHost = zoomHost; }
    
    // Validation
    bool isValid() const {
        return !m_sdkKey.empty() && !m_sdkSecret.empty();
    }
};

#endif //HEADLESS_ZOOM_BOT_SDK_CONFIG_H
