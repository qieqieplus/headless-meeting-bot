# Headless Meeting Bot Demo

This directory hosts the assets and scripts for the app replay/editor that runs on the HLS recordings produced by the bot. The `app/` folder contains a React + Vite application that understands manifests, per-user audio tracks, and events so you can explore recordings with precise timelines.

## Quick start

1. **Record a meeting:**
   ```bash
   cd demo
   ./recording.py record <meeting_id> --password <password>
   ```
   This connects to the API server's WebSocket streams (video, audio, events), records everything in real-time, and generates a manifest JSON file automatically.
   
   Recordings are organized in session directories: `recordings/{meeting_id}_{timestamp}/`

   **Note:** For local recording without a server, you can run the native bot executable directly:
   ```bash
   cd src/build
   ./headless_zoom_bot_c video <meeting_id> <password>
   ```
   However, this only saves HLS video files. For complete recordings with manifest and audio, use `recording.py` with the API server.

2. **Install and build the app**
   ```bash
   cd demo/app
   npm install
   npm run build
   cd ..
   ```

3. **Serve the app + recordings**
   ```bash
   ./serve.sh
   ```
   That command:
   - Automatically finds the most recent recording session
   - Builds the app (runs `npm run build`)
   - Copies built app files to demo root
   - Creates symlinks for video, audio, and manifest
   - Serves files from demo root over HTTP
   
   Or serve a specific recording:
   ```bash
   ./serve.sh recordings/{meeting_id}_{timestamp}
   ```

4. **Browse the experience**
   - The script prints a URL with the manifest parameter when it starts
   - Open that URL (e.g., `http://localhost:8000?manifest=/recordings/123456789_20250118_120000/manifest.json`)
   - The app loads the manifest and plays the HLS stream
   - Use the playback controls, timeline, and audio mixer to explore the recording

## Script reference

### `recording.py`

Multi-purpose script for recording and managing meeting recordings. Supports two sub-commands: `record` and `clean`.

#### Record Command

Usage: `./recording.py record <meeting_id> [options]`

Records a meeting from the API server's WebSocket streams and generates a manifest. Connects to video, audio, and events streams in real-time and saves everything to disk in a session-based directory structure.

**Arguments:**
- `meeting_id`: Meeting ID to record (required)

**Options:**
- `--output-dir DIR`: Custom output directory (default: auto-generated `recordings/{meeting_id}_{timestamp}`)
- `--host HOST`: Server host (default: `localhost`)
- `--port PORT`: Server port (default: `8080`)
- `--password PASSWORD`: Meeting password (optional)
- `--display-name NAME`: Display name when joining (default: `RecordingBot`)

**Examples:**
```bash
# Record a meeting (creates recordings/{meeting_id}_{timestamp}/)
./recording.py record 123456789 --password mypass

# Record to custom directory
./recording.py record 123456789 --output-dir ./my-recording

# Connect to remote server
./recording.py record 123456789 --host example.com --port 8080
```

**Recording structure:**
```
recordings/
└── {meeting_id}_{timestamp}/
    ├── audio/
    │   └── {user_id}/
    │       └── {type}_{timestamp}.{mp3|aac}
    ├── video/
    │   ├── *.ts (segments)
    │   └── *.m3u8 (playlists)
    └── manifest.json
```

The script:
- Joins the meeting via REST API
- Connects to WebSocket streams (video, audio, events) in parallel
- Records video HLS playlists and segments
- Records audio as encoded segments (MP3/AAC) with 64-byte filename headers
- Fetches final `manifest.json` from server API when recording ends

**Note:** This script records in real-time. Press Ctrl+C to stop recording. The manifest is fetched automatically from the server when the script exits.

#### Clean Command

Usage: `./recording.py clean [options]`

Clean up recording sessions.

**Options:**
- `--recordings-dir DIR`: Recordings base directory (default: `recordings`)
- `--pattern PATTERN`: Only delete sessions matching this pattern
- `--force`, `-f`: Delete without confirmation

**Examples:**
```bash
# Clean all sessions (with confirmation)
./recording.py clean

# Clean specific meeting sessions
./recording.py clean --pattern 123456789

# Clean without confirmation
./recording.py clean --force
```

### `serve.sh`

Usage: `./serve.sh [recording_dir] [port]`

- `recording_dir`: Path to recording session directory (default: `recordings`, automatically finds most recent session)
- `port`: Port for the app (default: `8000`)

**Special behavior:** If `recording_dir` is set to `recordings` (default), the script automatically finds and serves the most recent recording session.

**Examples:**
```bash
# Serve most recent recording
./serve.sh

# Serve specific recording session
./serve.sh recordings/123456789_20250118_120000

# Serve on custom port
./serve.sh recordings 3000
```

The script:
1. Validates the recording directory and manifest
2. Builds the React app (runs `npm run build`)
3. Copies built app files to demo root
4. Serves everything from the demo directory via Python HTTP server
5. Prints the manifest URL to open in the browser

**Clean command:** Use `./serve.sh clean` to remove generated files (assets, built files, etc.)

**Note:** The app loads the manifest from the URL parameter `?manifest=...`. The script prints the correct URL when it starts.

## Files and Directories

- `app/` – React + Vite app that powers the replay experience
- `recordings/` – Base directory for all recording sessions (auto-created)
  - `{meeting_id}_{timestamp}/` – Individual recording session
    - `video/` – HLS video files (*.ts segments, *.m3u8 playlists)
    - `audio/` – Audio segments organized by user ID (*.mp3 or *.aac files)
    - `manifest.json` – Recording metadata from server (includes embedded events)
- `recording.py` – Records meetings from API server and manages recordings  
- `serve.sh` – Builds and serves the app with recording assets

## Manifest format

The manifest is fetched from the server API (`GET /api/meetings/{meeting_id}/manifest`) and contains all recording metadata, timeline information, and events.

- **Top-level fields**
  - `meeting_id`: Meeting identifier
  - `t0`: Recording start timestamp (Unix milliseconds)
  - `duration`: Total duration in milliseconds
  - `participants[]`: List of participants with `id`, `name`, `role`, etc.
  - `events[]`: Embedded event array (no external file)
  - `metadata`: Additional metadata
- **Video**
  - `video[]`: Array of video tracks where each item has:
    - `id`: stable identifier
    - `kind`: `share` or `camera`
    - `userId`: Zoom user id (string)
    - `m3u8`: playlist path (relative to session directory)
    - `offsetMs`: start time on the media timeline
    - `segments[]`: optional visibility windows `{ startMs, endMs }`
- **Audio**
  - `audio[]`: Array of audio tracks with:
    - `id`: track identifier
    - `type`: `mixed`, `user`, or `share`
    - `user_id`: Zoom user id (string, only for user/share tracks)
    - `path`: Audio file path (e.g., `0/mixed_1700000000000.mp3`)
    - `format`: Object with `{ sample_rate, channels, encoding, bitrate }`
    - `timeline[]`: Segments with `{ start_pts, duration, offset_pts }`

Minimal example:

```json
{
  "meeting_id": "meeting123",
  "t0": 1700000000000,
  "duration": 120000,
  "video": [
    { "id": "share-1678-1763431200", "kind": "share", "userId": "1678", "m3u8": "video/share.m3u8", "offsetMs": 0 }
  ],
  "audio": [
    { 
      "id": "mixed", 
      "type": "mixed", 
      "path": "0/mixed_1700000000000.mp3",
      "format": { "sample_rate": 48000, "channels": 2, "encoding": "mp3", "bitrate": 128000 },
      "timeline": [{ "start_pts": 0, "duration": 120000, "offset_pts": 0 }]
    }
  ],
  "events": [
    { "type": "user_join", "user_id": "1678", "wall_ts": 1700000000000 }
  ],
  "participants": [
    { "id": "1678", "name": "Alice" }
  ]
}
```
