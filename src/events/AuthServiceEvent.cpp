#include "AuthServiceEvent.h"
#include "util/Logger.h"

AuthServiceEvent::AuthServiceEvent(std::function<void()> onAuth) {
    m_onAuth = std::move(onAuth);
}

void AuthServiceEvent::onAuthenticationReturn(ZOOMSDK::AuthResult result) {
    std::stringstream message;
    message << "authentication failed because the ";

    switch (result) {
        case ZOOMSDK::AUTHRET_KEYORSECRETEMPTY:
            message << "key or secret is empty";
            break;
        case ZOOMSDK::AUTHRET_JWTTOKENWRONG:
            message << "JWT is invalid";
            break;
        case ZOOMSDK::AUTHRET_OVERTIME:
            message << "operation timed out";
            break;
        case ZOOMSDK::AUTHRET_SUCCESS:
            if (m_onAuth) m_onAuth();
            else message << "authentication callback was not set";
            break;
        default:
            message << "Zoom SDK encountered an unknown error: " << result;
            break;
    }

    if (result != ZOOMSDK::AUTHRET_SUCCESS) {
        Util::Logger::getInstance().error(message.str());
        abort();
    }

    return;
}

void AuthServiceEvent::onLogout() {
    // Callback not implemented
}

void AuthServiceEvent::onZoomIdentityExpired() {
    // Callback not implemented
}

void AuthServiceEvent::onZoomAuthIdentityExpired() {
    // Callback not implemented
}

void AuthServiceEvent::onLoginReturnWithReason(ZOOMSDK::LOGINSTATUS ret, ZOOMSDK::IAccountInfo *pAccountInfo, ZOOMSDK::LoginFailReason reason) {
    // Callback not implemented
}
