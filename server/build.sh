#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
set -e

# --- Configuration ---
# Get the absolute path to the script's directory.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Path to the compiled C SDK library.
ZOOM_C_LIB_PATH="${PROJECT_ROOT}/src/build"

# Path to the official Zoom SDK libraries.
ZOOM_SDK_LIB_PATH="${PROJECT_ROOT}/lib/zoomsdk"

# Export paths and build flags as environment variables for cgo
export ZOOM_C_LIB_PATH="${ZOOM_C_LIB_PATH}"
export ZOOM_SDK_LIB_PATH="${ZOOM_SDK_LIB_PATH}"
export SRC_API_PATH="${PROJECT_ROOT}/src/c_api"
export BUILD_LIB_PATH="${ZOOM_C_LIB_PATH}"

# Set CGO flags for compilation and linking
export CGO_CFLAGS="-I${SRC_API_PATH}"
export CGO_LDFLAGS="-L${BUILD_LIB_PATH} -lzoomsdk_c -Wl,-rpath,${BUILD_LIB_PATH}"

# --- Build ---
echo "--- Building Go Server ---"
echo "Project Root: ${PROJECT_ROOT}"
echo "C SDK Library Path: ${ZOOM_C_LIB_PATH}"
echo "Zoom SDK Library Path: ${ZOOM_SDK_LIB_PATH}"
echo "Source API Path: ${SRC_API_PATH}"
echo "Build Library Path: ${BUILD_LIB_PATH}"
echo "CGO CFLAGS: ${CGO_CFLAGS}"
echo "CGO LDFLAGS: ${CGO_LDFLAGS}"
echo "Environment CGO variables set for build"
echo "------------------------"

cd "${SCRIPT_DIR}"

# Build the unified binary (includes both server and worker modes)
echo "Building headless-meeting-bot (unified binary)..."
CGO_CFLAGS="${CGO_CFLAGS}" CGO_LDFLAGS="${CGO_LDFLAGS}" go build -v -o headless-meeting-bot ./cmd/headless-meeting-bot

echo "--- Build Complete ---"
echo "Binary created: $(pwd)/headless-meeting-bot"
echo ""
echo "Usage:"
echo "  ./headless-meeting-bot server    # Start main server"
echo "  ./headless-meeting-bot worker    # Start worker (used internally by server)"
