#!/bin/sh

# Exit immediately if a command exits with a non-zero status.
set -e

echo "--- Building Headless Meeting Bot ---"
echo "Working directory: $(pwd)"
echo "Workspace contents:"
ls -la /workspace/

# Check if required directories exist
if [ ! -d "/workspace/server" ]; then
    echo "Error: /workspace/server not found. Please mount the project root to /workspace."
    exit 1
fi

if [ ! -d "/workspace/src" ]; then
    echo "Error: /workspace/src not found. Please mount the project root to /workspace."
    exit 1
fi

if [ ! -d "/workspace/lib" ]; then
    echo "Error: /workspace/lib not found. Please mount the project root to /workspace."
    exit 1
fi

# Important: Zoom SDK binaries are glibc-linked. Alpine uses musl.
# Build may succeed, but runtime on musl will fail to load glibc libs.
if [ ! -e "/lib/ld-linux-x86-64.so.2" ]; then
    echo "[warn] glibc dynamic loader not found. Consider using a Debian-based image for build/runtime."
fi

# Library directories used by CMake and Go linking
SDK_LIB="/workspace/lib/zoomsdk"
QT_LIB="/workspace/lib/zoomsdk/qt_libs/Qt/lib"
BUILD_LIB="/workspace/src/build"

# Ensure runtime loader can see SDK libs during configure/link
export LD_LIBRARY_PATH="${SDK_LIB}:${QT_LIB}:${BUILD_LIB}:${LD_LIBRARY_PATH}"

# Build the C SDK first
echo "--- Building C SDK ---"
cd /workspace/src
rm -rf build
mkdir -p build
cd build
cmake \
  -DCMAKE_BUILD_RPATH="${SDK_LIB};${QT_LIB};${BUILD_LIB}" \
  -DCMAKE_INSTALL_RPATH="${SDK_LIB};${QT_LIB};${BUILD_LIB}" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,-rpath,${SDK_LIB} -Wl,-rpath,${QT_LIB} -Wl,-rpath,${BUILD_LIB} -Wl,-rpath-link,${SDK_LIB} -Wl,-rpath-link,${QT_LIB} -Wl,-rpath-link,${BUILD_LIB}" \
  -DCMAKE_SHARED_LINKER_FLAGS="-Wl,-rpath,${SDK_LIB} -Wl,-rpath,${QT_LIB} -Wl,-rpath,${BUILD_LIB} -Wl,-rpath-link,${SDK_LIB} -Wl,-rpath-link,${QT_LIB} -Wl,-rpath-link,${BUILD_LIB}" \
  ..
# Determine parallel jobs in a POSIX-friendly way
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)"
make -j"${JOBS}"
echo "C SDK build completed"

# Build the Go application
echo "--- Building Go Application ---"
cd /workspace/server


# Download Go dependencies
export GOPROXY="https://goproxy.cn,direct"
go mod download

# Set up CGO environment variables
export CGO_CFLAGS="-I/workspace/src/c_api"
export CGO_LDFLAGS="-L${BUILD_LIB} -lzoomsdk_c -Wl,-rpath,${BUILD_LIB} -Wl,-rpath,${SDK_LIB} -Wl,-rpath,${QT_LIB} -Wl,-rpath-link,${SDK_LIB} -Wl,-rpath-link,${QT_LIB} -Wl,-rpath-link,${BUILD_LIB}"

# Build the application
CGO_ENABLED=1 GOOS=linux go build -a -installsuffix cgo -buildvcs=false -o headless-meeting-bot ./cmd/headless-meeting-bot

echo "--- Build Complete ---"
echo "Binary created: $(pwd)/headless-meeting-bot"
ls -la headless-meeting-bot

# Test the binary
echo "--- Testing Binary ---"
./headless-meeting-bot --help || echo "Binary built successfully (help command may not be implemented)"
