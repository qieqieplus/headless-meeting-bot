cmake_minimum_required(VERSION 3.20.2)
project(headless_zoom_bot)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_COMPILER /usr/bin/g++)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(ZOOM_SDK lib/zoomsdk)

find_path(JWT_CPP_INCLUDE_DIRS "lib/jwt-cpp/base.h")

find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET glib-2.0)

find_package(OpenSSL REQUIRED)

include_directories(${ZOOM_SDK}/h)
link_directories(${ZOOM_SDK} ${ZOOM_SDK}/qt_libs ${ZOOM_SDK}/qt_libs/Qt/lib)

# Original executable
add_executable(headless_zoom_bot src/demo.cpp
        src/ZoomSDK.cpp
        src/ZoomSDK.h
        src/Meeting.cpp
        src/Meeting.h
        src/SDKConfig.cpp
        src/SDKConfig.h
        src/MeetingConfig.cpp
        src/MeetingConfig.h
        src/events/AuthServiceEvent.cpp
        src/events/AuthServiceEvent.h
        src/events/MeetingServiceEvent.cpp
        src/events/MeetingServiceEvent.h
        src/events/MeetingReminderEvent.cpp
        src/events/MeetingReminderEvent.h
        src/events/MeetingRecordingCtrlEvent.cpp
        src/events/MeetingRecordingCtrlEvent.h
)

target_include_directories(headless_zoom_bot PRIVATE lib src)
target_link_libraries(headless_zoom_bot PRIVATE meetingsdk PkgConfig::deps ${OPENSSL_LIBRARIES})

# C API shared library
add_library(zoomsdk_c SHARED
        src/c_api/zoom_sdk_c.cpp
        src/c_api/zoom_sdk_c.h
        src/c_api/zoom_sdk_audio_delegate.cpp
        src/c_api/zoom_sdk_audio_delegate.h
        src/ZoomSDK.cpp
        src/ZoomSDK.h
        src/Meeting.cpp
        src/Meeting.h
        src/SDKConfig.cpp
        src/SDKConfig.h
        src/MeetingConfig.cpp
        src/MeetingConfig.h
        src/events/AuthServiceEvent.cpp
        src/events/AuthServiceEvent.h
        src/events/MeetingServiceEvent.cpp
        src/events/MeetingServiceEvent.h
        src/events/MeetingReminderEvent.cpp
        src/events/MeetingReminderEvent.h
        src/events/MeetingRecordingCtrlEvent.cpp
        src/events/MeetingRecordingCtrlEvent.h
)

target_include_directories(zoomsdk_c PRIVATE lib src)
target_link_libraries(zoomsdk_c PRIVATE meetingsdk PkgConfig::deps ${OPENSSL_LIBRARIES})

# Set output names for shared libraries
set_target_properties(zoomsdk_c PROPERTIES OUTPUT_NAME "zoomsdk_c")

# Set library version and soversion
set_target_properties(zoomsdk_c PROPERTIES VERSION 1.0.0 SOVERSION 1)

# C API example executable
add_executable(headless_zoom_bot_c src/c_api/demo.c)

target_include_directories(headless_zoom_bot_c PRIVATE src/c_api)
target_link_libraries(headless_zoom_bot_c PRIVATE zoomsdk_c PkgConfig::deps)
