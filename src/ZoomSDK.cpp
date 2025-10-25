#include "ZoomSDK.h"
#include "Meeting.h"
#include "MeetingConfig.h"
#include "util/Logger.h"
#include "util/Checks.h"
#include <cstdlib>

using namespace ZOOMSDK;

ZoomSDK::ZoomSDK() 
    : m_authService(nullptr)
    , m_settingService(nullptr)
    , m_networkHelper(nullptr)
    , m_meetingService(nullptr)
    , m_isInitialized(false)
    , m_isAuthenticated(false) {
}

ZoomSDK::~ZoomSDK() {
    cleanup();
}

SDKError ZoomSDK::initialize(const SDKConfig& config) {
    return initialize(config.sdkKey(), config.sdkSecret(), config.zoomHost());
}

SDKError ZoomSDK::initialize(const std::string& sdkKey, const std::string& sdkSecret, const std::string& zoomHost) {
    if (m_isInitialized) {
        return SDKERR_SUCCESS;
    }
    
    if (sdkKey.empty() || sdkSecret.empty()) {
        return SDKERR_UNINITIALIZE;
    }
    
    m_sdkKey = sdkKey;
    m_sdkSecret = sdkSecret;
    m_zoomHost = zoomHost;
    
    InitParam initParam;
    
    auto host = m_zoomHost.c_str();
    
    initParam.strWebDomain = host;
    initParam.strSupportUrl = host;
    
    initParam.emLanguageID = LANGUAGE_English;
    
    initParam.enableLogByDefault = true;
    initParam.enableGenerateDump = true;
    
    ZOOM_ERR_CHECK(InitSDK(initParam), "initialize SDK");
    
    ZOOM_ERR_CHECK(createGlobalServices(), "create global services");
    
    m_isInitialized = true;
    Util::Logger::getInstance().success("SDK initialized successfully");
    
    return SDKERR_SUCCESS;
}

SDKError ZoomSDK::createGlobalServices() {
    SDKError err;
    
    ZOOM_ERR_CHECK(CreateSettingService(&m_settingService), "create setting service");
    
    ZOOM_ERR_CHECK(CreateNetworkConnectionHelper(&m_networkHelper), "create network connection helper");
    
    // Configure proxy settings
    ProxySettings proxy_setting;
    proxy_setting.auto_detect = true;

    if (const char* proxy_env = getenv("HTTP_PROXY")) {
        proxy_setting.auto_detect = false;
        proxy_setting.proxy = proxy_env;
        Util::Logger::getInstance().info("Proxy found: " + std::string(proxy_env));
    }
    
    m_networkHelper->ConfigureProxy(proxy_setting);
    
    ZOOM_ERR_CHECK(CreateMeetingService(&m_meetingService), "create meeting service");
    
    return SDKERR_SUCCESS;
}

SDKError ZoomSDK::authenticate( std::function<void()> onAuthCallback) {
    if (!m_isInitialized) {
        return SDKERR_UNINITIALIZE;
    }
    
    if (m_isAuthenticated) {
        if (onAuthCallback) onAuthCallback();
        return SDKERR_SUCCESS;
    }
    
    SDKError err;
    
    ZOOM_ERR_CHECK(CreateAuthService(&m_authService), "create auth service");
    
    m_onAuthCallback = onAuthCallback;
    
     std::function<void()> onAuth = [this]() {
        m_isAuthenticated = true;
        Util::Logger::getInstance().success("SDK authenticated successfully");
        if (m_onAuthCallback) {
            m_onAuthCallback();
        }
    };
    
    m_authEvent = std::make_unique<AuthServiceEvent>(onAuth);
    ZOOM_ERR_CHECK(m_authService->SetEvent(m_authEvent.get()), "set auth event");
    
    generateJWT(m_sdkKey, m_sdkSecret);
    
    AuthContext ctx;
    ctx.jwt_token = m_jwt.c_str();
    
    return m_authService->SDKAuth(ctx);
}

void ZoomSDK::generateJWT(const std::string& key, const std::string& secret) {
    m_iat = std::chrono::system_clock::now();
    m_exp = m_iat + std::chrono::hours{24};
    
    m_jwt = jwt::create()
            .set_type("JWT")
            .set_issued_at(m_iat)
            .set_expires_at(m_exp)
            .set_payload_claim("appKey", jwt::claim(key))
            .set_payload_claim("tokenExp", jwt::claim(m_exp))
            .sign(jwt::algorithm::hs256{secret});
}

SDKError ZoomSDK::cleanup() {
    if (m_meetingService) {
        DestroyMeetingService(m_meetingService);
        m_meetingService = nullptr;
    }
    
    if (m_settingService) {
        DestroySettingService(m_settingService);
        m_settingService = nullptr;
    }
    
    if (m_authService) {
        m_authService->SetEvent(nullptr);
        DestroyAuthService(m_authService);
        m_authService = nullptr;
    }

    m_authEvent.reset();
    
    if (m_networkHelper) {
        DestroyNetworkConnectionHelper(m_networkHelper);
        m_networkHelper = nullptr;
    }
    
    if (m_isInitialized) {
        CleanUPSDK();
        m_isInitialized = false;
    }
    
    m_isAuthenticated = false;
    
    return SDKERR_SUCCESS;
}


bool ZoomSDK::hasError(const SDKError e, const std::string& action) {
    auto isError = e != SDKERR_SUCCESS;

    if(!action.empty()) {
        if (isError) {
            std::stringstream ss;
            ss << "failed to " << action << " with status " << e;
            Util::Logger::getInstance().error(ss.str());
        } else {
            Util::Logger::getInstance().success(action);
        }
    }
    return isError;
}
