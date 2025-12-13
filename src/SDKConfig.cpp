#include "SDKConfig.h"

SDKConfig::SDKConfig(const std::string& sdk_key, const std::string& sdk_secret,
                     const std::string& zoom_host)
    : sdk_key_(sdk_key), sdk_secret_(sdk_secret), zoom_host_(zoom_host) {}
