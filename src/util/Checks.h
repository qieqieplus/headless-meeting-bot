#ifndef HEADLESS_ZOOM_BOT_CHECKS_H
#define HEADLESS_ZOOM_BOT_CHECKS_H

#include <sstream>
#include <string>
#include <cstdlib>

#include "util/Logger.h"
#include "zoom_sdk.h"

// Expect a non-null pointer. Logs and aborts if null (fail-fast, unexpected state).
#define ASSERT_NOT_NULL(ptr) \
    do { \
        if (!(ptr)) { \
            Util::Logger::getInstance().error(std::string("NULL pointer: ") + #ptr); \
            std::abort(); \
        } \
    } while (0)

// Try an SDK call; on error, log and return the error code from the current function.
#define ZOOM_ERR_CHECK(expr, action) \
    do { \
        ZOOMSDK::SDKError _z_err = (expr); \
        if (_z_err != ZOOMSDK::SDKERR_SUCCESS) { \
            std::stringstream _z_ss; \
            _z_ss << "Failed to " << action << " with status " << _z_err; \
            Util::Logger::getInstance().error(_z_ss.str()); \
            return _z_err; \
        } \
    } while (0)

#endif // HEADLESS_ZOOM_BOT_CHECKS_H
