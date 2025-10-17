#include "SDKConfig.h"

SDKConfig::SDKConfig(const std::string& sdkKey,
                     const std::string& sdkSecret,
                     const std::string& zoomHost)
    : m_sdkKey(sdkKey)
    , m_sdkSecret(sdkSecret)
    , m_zoomHost(zoomHost) {
}
