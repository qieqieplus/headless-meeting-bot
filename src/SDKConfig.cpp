#include "SDKConfig.h"

SDKConfig::SDKConfig(const string& sdkKey,
                     const string& sdkSecret,
                     const string& zoomHost)
    : m_sdkKey(sdkKey)
    , m_sdkSecret(sdkSecret)
    , m_zoomHost(zoomHost) {
}
