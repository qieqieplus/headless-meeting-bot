cmake_minimum_required(VERSION 3.20.2)
project(headless_zoom_bot)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_COMPILER /usr/bin/g++)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)


set(ZOOM_SDK ../lib/zoomsdk)

find_path(JWT_CPP_INCLUDE_DIRS "../lib/jwt-cpp/base.h")

find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET glib-2.0)

find_package(OpenSSL REQUIRED)

include_directories(${ZOOM_SDK}/h)
link_directories(${ZOOM_SDK} ${ZOOM_SDK}/qt_libs ${ZOOM_SDK}/qt_libs/Qt/lib)

# Original executable
add_executable(headless_zoom_bot demo.cpp
        ZoomSDK.cpp
        ZoomSDK.h
        Meeting.cpp
        Meeting.h
        SDKConfig.cpp
        SDKConfig.h
        MeetingConfig.cpp
        MeetingConfig.h
        events/AuthServiceEvent.cpp
        events/AuthServiceEvent.h
        events/MeetingServiceEvent.cpp
        events/MeetingServiceEvent.h
        events/MeetingReminderEvent.cpp
        events/MeetingReminderEvent.h
        events/MeetingRecordingCtrlEvent.cpp
        events/MeetingRecordingCtrlEvent.h
        events/MeetingShareEvent.cpp
        events/MeetingShareEvent.h
)

target_include_directories(headless_zoom_bot PRIVATE ../lib .)
target_link_libraries(headless_zoom_bot PRIVATE meetingsdk PkgConfig::deps ${OPENSSL_LIBRARIES})

# C API shared library
add_library(zoomsdk_c SHARED
        c_api/zoom_sdk_c.cpp
        c_api/zoom_sdk_c.h
        c_api/zoom_sdk_audio_delegate.cpp
        c_api/zoom_sdk_audio_delegate.h
        c_api/zoom_sdk_video_delegate.cpp
        c_api/zoom_sdk_video_delegate.h
        ZoomSDK.cpp
        ZoomSDK.h
        Meeting.cpp
        Meeting.h
        SDKConfig.cpp
        SDKConfig.h
        MeetingConfig.cpp
        MeetingConfig.h
        events/AuthServiceEvent.cpp
        events/AuthServiceEvent.h
        events/MeetingServiceEvent.cpp
        events/MeetingServiceEvent.h
        events/MeetingReminderEvent.cpp
        events/MeetingReminderEvent.h
        events/MeetingRecordingCtrlEvent.cpp
        events/MeetingRecordingCtrlEvent.h
        events/MeetingShareEvent.cpp
        events/MeetingShareEvent.h
)

target_include_directories(zoomsdk_c PRIVATE ../lib .)
target_link_libraries(zoomsdk_c PRIVATE meetingsdk PkgConfig::deps ${OPENSSL_LIBRARIES})

# Set output names for shared libraries
set_target_properties(zoomsdk_c PROPERTIES OUTPUT_NAME "zoomsdk_c")

# Set library version and soversion
set_target_properties(zoomsdk_c PROPERTIES VERSION 1.0.0 SOVERSION 1)

# C API example executable (unified demo supporting audio/video/share modes)
add_executable(headless_zoom_bot_c c_api/demo.c)
target_include_directories(headless_zoom_bot_c PRIVATE c_api)
target_link_libraries(headless_zoom_bot_c PRIVATE zoomsdk_c PkgConfig::deps)
