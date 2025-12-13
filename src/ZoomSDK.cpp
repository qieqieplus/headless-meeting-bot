#include "ZoomSDK.h"

#include <cstdlib>

#include "Meeting.h"
#include "MeetingConfig.h"
#include "util/Checks.h"
#include "util/Logger.h"

using namespace ZOOMSDK;

ZoomSDK::ZoomSDK()
    : auth_service_(nullptr),
      setting_service_(nullptr),
      network_helper_(nullptr),
      meeting_service_(nullptr),
      is_initialized_(false),
      is_authenticated_(false) {}

ZoomSDK::~ZoomSDK() noexcept { (void)Cleanup(); }

SDKError ZoomSDK::Initialize(const SDKConfig& config) {
  return Initialize(config.SdkKey(), config.SdkSecret(), config.ZoomHost());
}

SDKError ZoomSDK::Initialize(const std::string& sdk_key, const std::string& sdk_secret,
                             const std::string& zoom_host) {
  if (is_initialized_) {
    return SDKERR_SUCCESS;
  }

  if (sdk_key.empty() || sdk_secret.empty()) {
    return SDKERR_UNINITIALIZE;
  }

  sdk_key_ = sdk_key;
  sdk_secret_ = sdk_secret;
  zoom_host_ = zoom_host;

  InitParam init_param;

  auto host = zoom_host_.c_str();

  init_param.strWebDomain = host;
  init_param.strSupportUrl = host;

  init_param.emLanguageID = LANGUAGE_English;

  init_param.enableLogByDefault = true;
  init_param.enableGenerateDump = true;

  ZOOM_ERR_CHECK(InitSDK(init_param), "initialize SDK");

  ZOOM_ERR_CHECK(CreateGlobalServices(), "create global services");

  is_initialized_ = true;
  Logger::GetInstance().Success("SDK initialized successfully");

  return SDKERR_SUCCESS;
}

SDKError ZoomSDK::CreateGlobalServices() {
  SDKError err;

  ZOOM_ERR_CHECK(CreateSettingService(&setting_service_), "create setting service");

  ZOOM_ERR_CHECK(CreateNetworkConnectionHelper(&network_helper_),
                 "create network connection helper");

  // Configure proxy settings
  ProxySettings proxy_setting;
  proxy_setting.auto_detect = true;

  if (const char* proxy_env = getenv("HTTP_PROXY")) {
    proxy_setting.auto_detect = false;
    proxy_setting.proxy = proxy_env;
    Logger::GetInstance().Info("Proxy found: " + std::string(proxy_env));
  }

  network_helper_->ConfigureProxy(proxy_setting);

  ZOOM_ERR_CHECK(CreateMeetingService(&meeting_service_), "create meeting service");

  return SDKERR_SUCCESS;
}

SDKError ZoomSDK::Authenticate(std::function<void()> on_auth_callback) {
  if (!is_initialized_) {
    return SDKERR_UNINITIALIZE;
  }

  if (is_authenticated_) {
    if (on_auth_callback) {
      on_auth_callback();
    }
    return SDKERR_SUCCESS;
  }

  SDKError err;

  ZOOM_ERR_CHECK(CreateAuthService(&auth_service_), "create auth service");

  on_auth_callback_ = on_auth_callback;

  std::function<void()> on_auth = [this]() {
    is_authenticated_ = true;
    Logger::GetInstance().Success("SDK authenticated successfully");
    if (on_auth_callback_) {
      on_auth_callback_();
    }
  };

  auth_event_ = std::make_unique<AuthServiceEvent>(on_auth);
  ZOOM_ERR_CHECK(auth_service_->SetEvent(auth_event_.get()), "set auth event");

  GenerateJwt(sdk_key_, sdk_secret_);

  AuthContext ctx;
  ctx.jwt_token = jwt_.c_str();

  return auth_service_->SDKAuth(ctx);
}

void ZoomSDK::GenerateJwt(const std::string& key, const std::string& secret) {
  iat_ = std::chrono::system_clock::now();
  exp_ = iat_ + std::chrono::hours{24};

  jwt_ = jwt::create()
             .set_type("JWT")
             .set_issued_at(iat_)
             .set_expires_at(exp_)
             .set_payload_claim("appKey", jwt::claim(key))
             .set_payload_claim("tokenExp", jwt::claim(exp_))
             .sign(jwt::algorithm::hs256{secret});
}

SDKError ZoomSDK::Cleanup() noexcept {
  if (meeting_service_) {
    DestroyMeetingService(meeting_service_);
    meeting_service_ = nullptr;
  }

  if (setting_service_) {
    DestroySettingService(setting_service_);
    setting_service_ = nullptr;
  }

  if (auth_service_) {
    auth_service_->SetEvent(nullptr);
    DestroyAuthService(auth_service_);
    auth_service_ = nullptr;
  }

  auth_event_.reset();
  on_auth_callback_ = nullptr;

  if (network_helper_) {
    DestroyNetworkConnectionHelper(network_helper_);
    network_helper_ = nullptr;
  }

  if (is_initialized_) {
    // avoid Zoom internal bugs
    // CleanUPSDK();
    is_initialized_ = false;
  }

  is_authenticated_ = false;
  jwt_.clear();

  return SDKERR_SUCCESS;
}
