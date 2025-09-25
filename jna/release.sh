#!/bin/bash
set -e

# Set the project root, which is one level up from the script's directory.
PROJECT_ROOT=$(dirname "$0")/..

# Navigate to the jna directory where the build.gradle is located.
cd "$(dirname "$0")"

echo "Building JNA library..."
# Clean previous builds and build the project.
gradle clean build

# Create a release directory if it doesn't exist.
RELEASE_DIR="release"
mkdir -p $RELEASE_DIR

# The JAR file is created in build/libs/.
# The name is based on the project name and version from build.gradle.
# Let's find it. It should be the only jar.
JAR_FILE=$(find build/libs -name "zoom-sdk-jna*.jar")

if [ -z "$JAR_FILE" ]; then
    echo "Error: JAR file not found in build/libs/"
    exit 1
fi

echo "Copying JAR file to $RELEASE_DIR..."
cp "$JAR_FILE" "$RELEASE_DIR/"

# The native library is now packaged inside the JAR, so we don't need to copy it separately.
# NATIVE_LIB_PATH="$PROJECT_ROOT/build/libzoomsdk_c.so"
#
# if [ -f "$NATIVE_LIB_PATH" ]; then
#     echo "Copying native library to $RELEASE_DIR..."
#     cp "$NATIVE_LIB_PATH" "$RELEASE_DIR/"
# else
#     echo "Warning: Native library ($NATIVE_LIB_PATH) not found. The JNA library may not run without it."
# fi

echo "Release artifacts are in the $RELEASE_DIR directory."
echo "Done."
