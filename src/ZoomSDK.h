#ifndef HEADLESS_ZOOM_BOT_ZOOM_SDK_H
#define HEADLESS_ZOOM_BOT_ZOOM_SDK_H

#include <iostream>
#include <chrono>
#include <string>
#include <sstream>
#include <functional>
#include <memory>

#include <jwt-cpp/jwt.h>

#include "SDKConfig.h"

#include "zoom_sdk.h"
#include "auth_service_interface.h"
#include "setting_service_interface.h"
#include "network_connection_handler_interface.h"
#include "meeting_service_interface.h"

#include "events/AuthServiceEvent.h"


typedef std::chrono::time_point<std::chrono::system_clock> time_point;

// Forward declarations
class Meeting;
class MeetingConfig;

class ZoomSDK {
    
    std::string m_jwt;
    time_point m_iat;
    time_point m_exp;
    
    ZOOMSDK::IAuthService* m_authService;
    ZOOMSDK::ISettingService* m_settingService;
    ZOOMSDK::INetworkConnectionHelper* m_networkHelper;
    ZOOMSDK::IMeetingService* m_meetingService;
    std::unique_ptr<AuthServiceEvent> m_authEvent;
    
    std::string m_sdkKey;
    std::string m_sdkSecret;
    std::string m_zoomHost;
    
    bool m_isInitialized;
    bool m_isAuthenticated;
    
    std::function<void()> m_onAuthCallback;
    
    ZOOMSDK::SDKError createGlobalServices();
    void generateJWT(const std::string& key, const std::string& secret);
    
public:
    ZoomSDK();
    ~ZoomSDK();
    
    ZOOMSDK::SDKError initialize(const SDKConfig& config);
    ZOOMSDK::SDKError initialize(const std::string& sdkKey, const std::string& sdkSecret, const std::string& zoomHost = "https://zoom.us");
    ZOOMSDK::SDKError authenticate(std::function<void()> onAuthCallback = nullptr);
    
    ZOOMSDK::SDKError cleanup();
    
    bool isInitialized() const { return m_isInitialized; }
    bool isAuthenticated() const { return m_isAuthenticated; }
    
    // Access to global services
    ZOOMSDK::ISettingService* getSettingService() const { return m_settingService; }
    ZOOMSDK::INetworkConnectionHelper* getNetworkHelper() const { return m_networkHelper; }
    ZOOMSDK::IMeetingService* getMeetingService() const { return m_meetingService; }
        
    // Utility methods
    static bool hasError(ZOOMSDK::SDKError e, const std::string& action="");
};

#endif //HEADLESS_ZOOM_BOT_ZOOM_SDK_H
