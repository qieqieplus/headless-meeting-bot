#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
set -e

# --- Configuration ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# Docker image configuration
IMAGE_NAME="headless-meeting-bot"
IMAGE_TAG="latest"
FULL_IMAGE_NAME="${IMAGE_NAME}:${IMAGE_TAG}"

# Parse command line arguments
BUILD_MODE="deploy"  # default to deploy
if [ "$1" = "build" ]; then
    BUILD_MODE="build"
elif [ "$1" = "deploy" ]; then
    BUILD_MODE="deploy"
elif [ "$1" = "help" ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    echo "Usage: $0 [build|deploy]"
    echo ""
    echo "  build  - Build Docker image that compiles the application"
    echo "  deploy - Build Docker image that runs pre-built binaries (default)"
    echo ""
    echo "Examples:"
    echo "  $0 build   # Build compilation image"
    echo "  $0 deploy  # Build runtime image"
    echo "  $0         # Build runtime image (default)"
    exit 0
fi

# --- Build Context Setup ---
echo "--- Preparing Docker Build Context ---"
echo "Project Root: ${PROJECT_ROOT}"
echo "Script Directory: ${SCRIPT_DIR}"
echo "Build Mode: ${BUILD_MODE}"

if [ "$BUILD_MODE" = "build" ]; then
    # For build mode, we mount source code
    echo "Building compilation image with mounted sources..."
    DOCKERFILE="${SCRIPT_DIR}/Dockerfile.build"
    IMAGE_NAME="${IMAGE_NAME}-build"
    FULL_IMAGE_NAME="${IMAGE_NAME}:${IMAGE_TAG}"
    
    # Check if source code exists
    if [ ! -f "${PROJECT_ROOT}/server/go.mod" ]; then
        echo "Error: Go module not found at ${PROJECT_ROOT}/server/go.mod" >&2
        echo "Please ensure you're running from the correct directory." >&2
        exit 1
    fi
    
    if [ ! -f "${PROJECT_ROOT}/src/CMakeLists.txt" ]; then
        echo "Error: C SDK source not found at ${PROJECT_ROOT}/src/CMakeLists.txt" >&2
        echo "Please ensure the C SDK source code is available." >&2
        exit 1
    fi
    
elif [ "$BUILD_MODE" = "deploy" ]; then
    # For deploy mode, we need pre-built binaries
    echo "Building runtime image..."
    DOCKERFILE="${SCRIPT_DIR}/Dockerfile"
    
    # Ensure required libraries exist
    ZOOM_C_LIB_PATH="${PROJECT_ROOT}/src/build"
    ZOOM_SDK_LIB_PATH="${PROJECT_ROOT}/lib/zoomsdk"

    if [ ! -f "${ZOOM_C_LIB_PATH}/libzoomsdk_c.so.1" ]; then
        echo "Error: C SDK library not found at ${ZOOM_C_LIB_PATH}/libzoomsdk_c.so.1" >&2
        echo "Please build the C SDK first by running:" >&2
        echo "  cd ${PROJECT_ROOT}/src && mkdir -p build && cd build" >&2
        echo "  cmake .. && make" >&2
        exit 1
    fi

    if [ ! -f "${ZOOM_SDK_LIB_PATH}/libmeetingsdk.so.1" ]; then
        echo "Error: Zoom SDK library not found at ${ZOOM_SDK_LIB_PATH}/libmeetingsdk.so.1" >&2
        echo "Please ensure the Zoom SDK is properly installed." >&2
        exit 1
    fi

    if [ ! -f "${PROJECT_ROOT}/server/headless-meeting-bot" ]; then
        echo "Error: Go binary not found at ${PROJECT_ROOT}/server/headless-meeting-bot" >&2
        echo "Please build the Go application first by running:" >&2
        echo "  cd ${PROJECT_ROOT}/server && ./build.sh" >&2
        exit 1
    fi
fi

# --- Docker Build ---
echo "--- Building Docker Image ---"
echo "Image Name: ${FULL_IMAGE_NAME}"
echo "Dockerfile: ${DOCKERFILE}"
echo "Build Context: ${PROJECT_ROOT}"
echo "------------------------"

if [ "$BUILD_MODE" = "build" ]; then
    # First build the stable build environment image
    echo "Building stable build environment..."
    docker build \
        -f "${DOCKERFILE}" \
        -t "${FULL_IMAGE_NAME}" \
        "${PROJECT_ROOT}/server"
    
    # Then run the build with mounted sources
    echo "Running build with mounted sources..."
    docker run --rm \
        -v "${PROJECT_ROOT}:/workspace" \
        "${FULL_IMAGE_NAME}"
else
    # For deploy mode, use traditional docker build
    docker build \
        -f "${DOCKERFILE}" \
        -t "${FULL_IMAGE_NAME}" \
        "${PROJECT_ROOT}"
fi

# --- Build Complete ---
echo "--- Docker Build Complete ---"
echo "Image: ${FULL_IMAGE_NAME}"

if [ "$BUILD_MODE" = "build" ]; then
    echo ""
    echo "Build completed successfully! The binary has been created at:"
    echo "  ${PROJECT_ROOT}/server/headless-meeting-bot"
    echo ""
    echo "Next steps:"
    echo "  1. Test the binary locally:"
    echo "     cd ${PROJECT_ROOT}/server && ./headless-meeting-bot server"
    echo ""
    echo "  2. Build the deploy image:"
    echo "     $0 deploy"
    
elif [ "$BUILD_MODE" = "deploy" ]; then
    echo ""
    echo "Deploy image created! You can now run:"
    echo "  # Run the container:"
    echo "  docker run -d --name meeting-bot \\"
    echo "    -p 8080:8080 \\"
    echo "    -e ZOOM_SDK_KEY=\"your_sdk_key\" \\"
    echo "    -e ZOOM_SDK_SECRET=\"your_sdk_secret\" \\"
    echo "    ${FULL_IMAGE_NAME}"
    echo ""
    echo "  # Run with custom settings:"
    echo "  docker run -d --name meeting-bot \\"
    echo "    -p 8080:8080 \\"
    echo "    -e ZOOM_SDK_KEY=\"your_sdk_key\" \\"
    echo "    -e ZOOM_SDK_SECRET=\"your_sdk_secret\" \\"
    echo "    -e HTTP_ADDR=\":9090\" \\"
    echo "    -e LOG_LEVEL=\"debug\" \\"
    echo "    -v \$(pwd)/recordings:/app/recordings \\"
    echo "    ${FULL_IMAGE_NAME}"
    echo ""
    echo "  # View logs:"
    echo "  docker logs -f meeting-bot"
    echo ""
    echo "  # Stop the container:"
    echo "  docker stop meeting-bot && docker rm meeting-bot"
fi
