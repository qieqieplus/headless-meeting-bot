#ifndef HEADLESS_ZOOM_BOT_AUTHSERVICEEVENT_H
#define HEADLESS_ZOOM_BOT_AUTHSERVICEEVENT_H

#include <iostream>
#include <sstream>
#include <functional>
#include "auth_service_interface.h"


class AuthServiceEvent : public ZOOMSDK::IAuthServiceEvent  {
    std::function<void()> m_onAuth;

public:
    AuthServiceEvent(std::function<void()> onAuth);
    ~AuthServiceEvent() {};

    /**
     * callback that is triggered when authentication is complete
     * @param result status of the authentication return
     */
    void onAuthenticationReturn(ZOOMSDK::AuthResult result) override;

    /**
     * Callback of login result with fail reason.
     * @param ret Login Status
     * @param pAccountInfo Account Info that is only valid when ret == LOGINRET_SUCCESS
     * @param reason Reason for login failure that is only valid when ret == LOGIN_FAILED
     */
    void onLoginReturnWithReason(ZOOMSDK::LOGINSTATUS ret, ZOOMSDK::IAccountInfo* pAccountInfo, ZOOMSDK::LoginFailReason reason) override;

    /**
     * Logout result callback.
     */
    void onLogout() override;

    /**
     * callback used when the Zoom identity has expired
     * when triggered, please re-login or generate a new zoom access token via REST Api.
     */
    void onZoomIdentityExpired() override;

    /**
     * callback used when Zoom authentication identity will be expired in 10 minutes
     * when triggered please re-auth
     */
    void onZoomAuthIdentityExpired() override;


};


#endif //HEADLESS_ZOOM_BOT_AUTHSERVICEEVENT_H
