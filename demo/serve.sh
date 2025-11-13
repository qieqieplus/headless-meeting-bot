#!/bin/bash

echo "=== Meeting Recordings Demo ==="

# Parameters
VIDEO_DIR="${1:-video}"
OUTPUT_JSON="${2:-m3u8.json}"

# Generate m3u8 files list JSON
echo "Generating m3u8 files list from '$VIDEO_DIR' into '$OUTPUT_JSON'..."
echo "[" > "$OUTPUT_JSON"
first=true
for file in "$VIDEO_DIR"/*.m3u8; do
    if [ -f "$file" ]; then
        if [ "$first" = true ]; then
            first=false
        else
            echo "," >> "$OUTPUT_JSON"
        fi
        echo "  \"$file\"" >> "$OUTPUT_JSON"
    fi
done
echo "]" >> "$OUTPUT_JSON"

echo "Available m3u8 files:"
cat "$OUTPUT_JSON" | python3 -m json.tool


# Start Python HTTP server
python3 -m http.server --bind localhost 10000
