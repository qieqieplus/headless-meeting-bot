cmake_minimum_required(VERSION 3.20.2)
project(headless_zoom_bot)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_COMPILER /usr/bin/g++)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(LIBRARY_PATH ../lib)
set(ZOOM_SDK ${LIBRARY_PATH}/zoomsdk)

find_path(JWT_CPP_INCLUDE_DIRS "${LIBRARY_PATH}/jwt-cpp/base.h")

find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)

find_package(OpenSSL REQUIRED)

include_directories(.)
include_directories(${LIBRARY_PATH})
include_directories(${ZOOM_SDK}/h)
link_directories(${ZOOM_SDK} ${ZOOM_SDK}/qt_libs ${ZOOM_SDK}/qt_libs/Qt/lib)

# C API shared library
add_library(zoomsdk_c SHARED
        c_api/zoom_sdk_c.cpp
        c_api/zoom_sdk_c.h
        c_api/zoom_sdk_internal.cpp
        c_api/zoom_sdk_internal.h
        c_api/zoom_sdk_audio_delegate.cpp
        c_api/zoom_sdk_audio_delegate.h
        c_api/zoom_sdk_video_delegate.cpp
        c_api/zoom_sdk_video_delegate.h
        video/VideoEncoder.cpp
        video/VideoEncoder.h
        video/AudioEncoder.cpp
        video/AudioEncoder.h
        video/HlsMuxer.cpp
        video/HlsMuxer.h
        video/AvioMemorySink.cpp
        video/AvioMemorySink.h
        video/MediaEncodePipeline.cpp
        video/MediaEncodePipeline.h
        ZoomSDK.cpp
        ZoomSDK.h
        Meeting.cpp
        Meeting.h
        MediaController.cpp
        MediaController.h
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

# Hide all symbols by default; only explicitly exported API remains visible
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|")
  target_compile_options(zoomsdk_c PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
endif()
target_compile_definitions(zoomsdk_c PRIVATE ZOOM_SDK_C_BUILD=1)

# Link FFmpeg (required)
target_link_libraries(zoomsdk_c PRIVATE
    meetingsdk
    PkgConfig::deps
    ${OPENSSL_LIBRARIES}
    PkgConfig::AVCODEC
    PkgConfig::AVFORMAT
    PkgConfig::AVUTIL
    PkgConfig::SWRESAMPLE
)

# Set output names for shared libraries
set_target_properties(zoomsdk_c PROPERTIES OUTPUT_NAME "zoomsdk_c")

# Set library version and soversion
set_target_properties(zoomsdk_c PROPERTIES VERSION 1.0.0 SOVERSION 1)

# C API example executable (unified demo supporting audio/video/share modes)
add_executable(headless_zoom_bot_c c_api/demo.c)
target_include_directories(headless_zoom_bot_c PRIVATE c_api)
target_link_libraries(headless_zoom_bot_c PRIVATE zoomsdk_c PkgConfig::deps)
