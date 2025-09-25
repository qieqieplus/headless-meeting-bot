#include "ZoomSDK.h"
#include "Meeting.h"
#include "MeetingConfig.h"
#include "util/Logger.h"
#include <cstdlib>

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

SDKError ZoomSDK::initialize(const string& sdkKey, const string& sdkSecret, const string& zoomHost) {
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
    
    SDKError err = InitSDK(initParam);
    if (hasError(err, "initialize SDK")) return err;
    
    err = createGlobalServices();
    if (hasError(err, "create global services")) return err;
    
    m_isInitialized = true;
    Util::Logger::getInstance().success("SDK initialized successfully");
    
    return SDKERR_SUCCESS;
}

SDKError ZoomSDK::createGlobalServices() {
    SDKError err;
    
    err = CreateSettingService(&m_settingService);
    if (hasError(err, "create setting service")) return err;
    
    err = CreateNetworkConnectionHelper(&m_networkHelper);
    if (hasError(err, "create network connection helper")) return err;
    
    // Configure proxy settings
    ProxySettings proxy_setting;
    proxy_setting.auto_detect = true;

    if (const char* proxy_env = getenv("HTTP_PROXY")) {
        proxy_setting.auto_detect = false;
        proxy_setting.proxy = proxy_env;
        Util::Logger::getInstance().info("Proxy found: " + string(proxy_env));
    }
    
    m_networkHelper->ConfigureProxy(proxy_setting);
    
    // Create meeting service at SDK level
    err = CreateMeetingService(&m_meetingService);
    if (hasError(err, "create meeting service")) return err;
    
    return SDKERR_SUCCESS;
}

SDKError ZoomSDK::authenticate(function<void()> onAuthCallback) {
    if (!m_isInitialized) {
        return SDKERR_UNINITIALIZE;
    }
    
    if (m_isAuthenticated) {
        if (onAuthCallback) onAuthCallback();
        return SDKERR_SUCCESS;
    }
    
    SDKError err;
    
    err = CreateAuthService(&m_authService);
    if (hasError(err, "create auth service")) return err;
    
    m_onAuthCallback = onAuthCallback;
    
    function<void()> onAuth = [&]() {
        m_isAuthenticated = true;
        Util::Logger::getInstance().success("SDK authenticated successfully");
        if (m_onAuthCallback) {
            m_onAuthCallback();
        }
    };
    
    err = m_authService->SetEvent(new AuthServiceEvent(onAuth));
    if (hasError(err, "set auth event")) return err;
    
    generateJWT(m_sdkKey, m_sdkSecret);
    
    AuthContext ctx;
    ctx.jwt_token = m_jwt.c_str();
    
    return m_authService->SDKAuth(ctx);
}

void ZoomSDK::generateJWT(const string& key, const string& secret) {
    m_iat = std::chrono::system_clock::now();
    m_exp = m_iat + std::chrono::hours{24};
    
    m_jwt = jwt::create()
            .set_type("JWT")
            .set_issued_at(m_iat)
            .set_expires_at(m_exp)
            .set_payload_claim("appKey", claim(key))
            .set_payload_claim("tokenExp", claim(m_exp))
            .sign(algorithm::hs256{secret});
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
        DestroyAuthService(m_authService);
        m_authService = nullptr;
    }
    
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


bool ZoomSDK::hasError(const SDKError e, const string& action) {
    auto isError = e != SDKERR_SUCCESS;

    if(!action.empty()) {
        if (isError) {
            stringstream ss;
            ss << "failed to " << action << " with status " << e;
            Util::Logger::getInstance().error(ss.str());
        } else {
            Util::Logger::getInstance().success(action);
        }
    }
    return isError;
}
