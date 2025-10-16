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

using namespace std;
using namespace jwt;
using namespace ZOOMSDK;

typedef chrono::time_point<chrono::system_clock> time_point;

// Forward declarations
class Meeting;
class MeetingConfig;

class ZoomSDK {
    
    string m_jwt;
    time_point m_iat;
    time_point m_exp;
    
    IAuthService* m_authService;
    ISettingService* m_settingService;
    INetworkConnectionHelper* m_networkHelper;
    IMeetingService* m_meetingService;
    std::unique_ptr<AuthServiceEvent> m_authEvent;
    
    string m_sdkKey;
    string m_sdkSecret;
    string m_zoomHost;
    
    bool m_isInitialized;
    bool m_isAuthenticated;
    
    function<void()> m_onAuthCallback;
    
    SDKError createGlobalServices();
    void generateJWT(const string& key, const string& secret);
    
public:
    ZoomSDK();
    ~ZoomSDK();
    
    SDKError initialize(const SDKConfig& config);
    SDKError initialize(const string& sdkKey, const string& sdkSecret, const string& zoomHost = "https://zoom.us");
    SDKError authenticate(function<void()> onAuthCallback = nullptr);
    
    SDKError cleanup();
    
    bool isInitialized() const { return m_isInitialized; }
    bool isAuthenticated() const { return m_isAuthenticated; }
    
    // Access to global services
    ISettingService* getSettingService() const { return m_settingService; }
    INetworkConnectionHelper* getNetworkHelper() const { return m_networkHelper; }
    IMeetingService* getMeetingService() const { return m_meetingService; }
        
    // Utility methods
    static bool hasError(SDKError e, const string& action="");
};

#endif //HEADLESS_ZOOM_BOT_ZOOM_SDK_H
