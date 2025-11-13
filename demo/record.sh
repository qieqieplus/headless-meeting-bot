#!/bin/bash

# Headless Zoom Meeting Bot Recording Script
# Usage: ./record.sh <meeting_id> <password>

set -e  # Exit on any error

# Configuration - can be overridden with environment variables
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="${SRC_DIR:-$SCRIPT_DIR/../src}"
BUILD_DIR="${BUILD_DIR:-$SRC_DIR/build}"
EXECUTABLE="${EXECUTABLE:-$BUILD_DIR/headless_zoom_bot_c}"
OUTPUT_DIR="${OUTPUT_DIR:-video}"

# Load environment variables from .env file if it exists
ENV_FILE="${ENV_FILE:-$SCRIPT_DIR/.env}"
if [ -f "$ENV_FILE" ]; then
    echo "Loading environment variables from $ENV_FILE"
    set -a  # automatically export all variables
    source "$ENV_FILE"
    set +a
fi

# Show help if requested
if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    echo "Headless Zoom Meeting Bot Recording Script"
    echo ""
    echo "Usage: $0 <meeting_id> <password>"
    echo ""
    echo "Environment Variables (optional):"
    echo "  ENV_FILE        - Path to .env file (default: $SCRIPT_DIR/.env)"
    echo "  SRC_DIR         - Source directory (default: $SCRIPT_DIR/../src)"
    echo "  BUILD_DIR       - Build directory (default: \$SRC_DIR/build)"
    echo "  EXECUTABLE      - Bot executable path (default: \$BUILD_DIR/headless_zoom_bot_c)"
    echo "  OUTPUT_DIR      - Output directory name (default: video)"
    echo "  ZOOM_SDK_KEY    - Zoom SDK key (required)"
    echo "  ZOOM_SDK_SECRET - Zoom SDK secret (required)"
    echo ""
    echo "Examples:"
    echo "  $0 123456789 123456"
    echo "  OUTPUT_DIR=recordings $0 123456789 123456"
    exit 0
fi

# Check arguments
if [ $# -ne 2 ]; then
    echo "Usage: $0 <meeting_id> <password>"
    echo "Use '$0 --help' for more information"
    exit 1
fi

MEETING_ID="$1"
PASSWORD="$2"

echo "=== Headless Meeting Bot ==="
echo "Meeting ID: $MEETING_ID"
echo "Output Dir: $OUTPUT_DIR"
echo "Executable: $EXECUTABLE"
echo ""

# Check if executable exists, build if not
if [ ! -f "$EXECUTABLE" ]; then
    echo "Building headless_zoom_bot_c..."

    # Create build directory if it doesn't exist
    mkdir -p "$BUILD_DIR"

    # Build with cmake
    cd "$BUILD_DIR"
    echo "Building in: $(pwd)"
    cmake "$SRC_DIR"
    make -j$(nproc)
    cd "$SCRIPT_DIR"
fi

# Verify executable exists
if [ ! -f "$EXECUTABLE" ]; then
    echo "Error: Executable not found at $EXECUTABLE"
    exit 1
fi

# Check for required environment variables
if [ -z "$ZOOM_SDK_KEY" ] || [ -z "$ZOOM_SDK_SECRET" ]; then
    echo "Error: ZOOM_SDK_KEY and ZOOM_SDK_SECRET environment variables must be set"
    echo ""
    echo "Example:"
    echo "export ZOOM_SDK_KEY='your_sdk_key'"
    echo "export ZOOM_SDK_SECRET='your_sdk_secret'"
    exit 1
fi

# Create output directory if it doesn't exist
mkdir -p "$OUTPUT_DIR"

# Change to output directory so HLS files are written there
cd "$OUTPUT_DIR"

# Run the bot with video mode
echo "Starting recording for meeting $MEETING_ID..."
echo "HLS files will be saved in $(pwd)/"
echo ""

"$EXECUTABLE" video "$MEETING_ID" "$PASSWORD"
