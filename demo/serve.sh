#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Handle clean option
if [ "${1:-}" = "clean" ]; then
  echo "=== Cleaning generated files ==="
  
  # Remove copied assets
  [ -d "$SCRIPT_DIR/assets" ] && rm -rf "$SCRIPT_DIR/assets" && echo "Removed assets/"
  
  # Remove copied files
  [ -f "$SCRIPT_DIR/index.html" ] && rm -f "$SCRIPT_DIR/index.html" && echo "Removed index.html"
  [ -f "$SCRIPT_DIR/vite.svg" ] && rm -f "$SCRIPT_DIR/vite.svg" && echo "Removed vite.svg"
  
  # Clean app build artifacts (optional)
  APP_DIR="$SCRIPT_DIR/app"
  if [ -d "$APP_DIR" ]; then
    [ -d "$APP_DIR/dist" ] && rm -rf "$APP_DIR/dist" && echo "Removed app/dist/"
    [ -d "$APP_DIR/node_modules" ] && rm -rf "$APP_DIR/node_modules" && echo "Removed app/node_modules/"
  fi
  
  echo "Clean complete!"
  exit 0
fi

RECORDING_DIR="${1:-recordings}"
PORT="${2:-8000}"

echo "=== Meeting Recordings App Demo ==="

# Resolve recording directory path
BASE_RECORDING_DIR="$SCRIPT_DIR/$RECORDING_DIR"
if [ ! -d "$BASE_RECORDING_DIR" ]; then
  echo "Error: recording directory '$BASE_RECORDING_DIR' not found"
  exit 1
fi

SESSION_PATH="$BASE_RECORDING_DIR"
MANIFEST_PATH="$SESSION_PATH/manifest.json"

# If manifest missing at base, try to find most recent session
if [ ! -f "$MANIFEST_PATH" ] && [ "$RECORDING_DIR" = "recordings" ]; then
  LATEST_SESSION=$(find "$BASE_RECORDING_DIR" -mindepth 1 -maxdepth 1 -type d -printf '%T@ %p\n' | sort -rn | head -1 | cut -d' ' -f2-)
  if [ -n "$LATEST_SESSION" ]; then
    SESSION_PATH="$LATEST_SESSION"
    MANIFEST_PATH="$SESSION_PATH/manifest.json"
    echo "Using most recent recording: ${SESSION_PATH#$SCRIPT_DIR/}"
  fi
fi

if [ ! -f "$MANIFEST_PATH" ]; then
  echo "Error: manifest not found at $MANIFEST_PATH"
  echo "Generate one with: ./recording.py record <meeting_id>"
  exit 1
fi

echo "Recording directory: ${SESSION_PATH#$SCRIPT_DIR/}"
echo "Manifest: ${MANIFEST_PATH#$SCRIPT_DIR/}"

# Build app
APP_DIR="$SCRIPT_DIR/app"
if [ ! -d "$APP_DIR" ]; then
  echo "Error: app directory '$APP_DIR' missing"
  exit 1
fi

[ ! -d "$APP_DIR/node_modules" ] && (cd "$APP_DIR" && npm install)

echo "Building app..."
(cd "$APP_DIR" && npm run build)

DIST_DIR="$APP_DIR/dist"
if [ ! -d "$DIST_DIR" ]; then
  echo "Error: app dist directory '$DIST_DIR' not found after build"
  exit 1
fi

MANIFEST_HTTP_PATH="/${MANIFEST_PATH#$SCRIPT_DIR/}"

# Copy built files to demo root for serving
echo "Copying built files to demo root..."
cp -r "$DIST_DIR"/assets "$SCRIPT_DIR/" 2>/dev/null || true
cp "$DIST_DIR"/index.html "$SCRIPT_DIR/" 2>/dev/null || true
cp "$DIST_DIR"/vite.svg "$SCRIPT_DIR/" 2>/dev/null || true

echo ""
echo "Serving app on http://localhost:$PORT"
echo "Recording: ${SESSION_PATH#$SCRIPT_DIR/}"
echo "  - Manifest: $MANIFEST_HTTP_PATH"
echo "  - Video: /${SESSION_PATH#$SCRIPT_DIR/}/video/"
echo "  - Audio: /${SESSION_PATH#$SCRIPT_DIR/}/audio/"
echo ""
echo "Open: http://localhost:$PORT?manifest=$MANIFEST_HTTP_PATH"
echo ""

cd "$SCRIPT_DIR"
python3 -m http.server --bind 0.0.0.0 "$PORT"
