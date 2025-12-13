# Headless Meeting Bot - Go Server

For setup and deployment instructions, see the [main README](../README.md).

## API Documentation

> **⚠️ IMPORTANT: Ephemeral / Real-time Only**
> This API provides real-time access to meeting data. All streams and state are **ephemeral**.
> When the meeting ends, the worker process terminates, and all data (audio, video, events) is **lost immediately**.
> Clients **MUST** consume and save data in real-time. There is no persistent storage or playback capability after the meeting ends.

### Join Token Feature

The `join_token` parameter enables automatic recording authorization when joining meetings. This is useful for scenarios where you need to automatically start recording without manual intervention:

- **Purpose**: Allows the bot to automatically start recording when joining a meeting
- **Usage**: Include the `join_token` field in your POST request to `/api/meetings`
- **Note**: The join token is obtained from Zoom's API or SDK configuration for recording-enabled applications

### REST API

#### `POST /api/meetings`

Join a meeting.

**Request Body Fields:**

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `meeting_id` | string | Yes | - | The meeting ID to join |
| `password` | string | No | - | Meeting password if required |
| `display_name` | string | No | "Recording Bot" | Display name in the meeting |
| `join_token` | string | No | - | Join token for automatic recording authorization |
| `enable_audio` | boolean | No | `true`* | Request audio capture |
| `enable_video` | boolean | No | `false` | Request HLS video capture |

*If both `enable_audio` and `enable_video` are `false` or omitted, the server will implicitly enable audio recording to ensure the bot joins with recording privileges.

**Response:** `202 Accepted`

| Field | Type | Description |
|-------|------|-------------|
| `status` | string | Always `"joining"` |

**Error Responses:**

| Status Code | Error Field | Description |
|-------------|-------------|-------------|
| `400 Bad Request` | `"Invalid request body"` | Malformed JSON in request body |
| `400 Bad Request` | `"Meeting ID is required"` | Missing `meeting_id` field |
| `400 Bad Request` | `"meeting already exists"` | Meeting is already active |
| `500 Internal Server Error` | Error message | Server error during join operation |

**Example Request:**

```bash
curl -X POST http://localhost:8080/api/meetings \
  -H "Content-Type: application/json" \
  -d '{
    "meeting_id": "1234567890",
    "password": "password",
    "display_name": "Recording Bot",
    "join_token": "your_token_here",
    "enable_audio": true,
    "enable_video": true
  }'
```

#### `DELETE /api/meetings/{meeting_id}`

Leave a meeting.

**Response:** `200 OK`

| Field | Type | Description |
|-------|------|-------------|
| `status` | string | Always `"left"` |

**Error Responses:**

| Status Code | Error Field | Description |
|-------------|-------------|-------------|
| `404 Not Found` | `"meeting not found"` | Meeting ID does not exist |
| `500 Internal Server Error` | Error message | Server error during leave operation |

#### `GET /api/meetings`

List all active meetings.

**Response:** `200 OK`

Array of meeting objects with the following structure:

| Field | Type | Description |
|-------|------|-------------|
| `meeting_id` | string | The meeting identifier |
| `status` | object | Status information (see StatusInfo structure below) |

**StatusInfo Structure:**

| Field | Type | Description |
|-------|------|-------------|
| `state` | string | Meeting state (see Meeting States table) |
| `detail` | integer | Status detail code from Zoom SDK |
| `error` | string | Error message (omitted if no error) |

**Meeting States:**

| State | Description |
|-------|-------------|
| `idle` | Meeting not started |
| `connecting` | Connecting to meeting |
| `in_meeting` | Successfully joined and in meeting |
| `reconnecting` | Reconnecting after connection loss |
| `failed` | Meeting join failed |
| `ended` | Meeting has ended |
| `unknown` | Unknown state |

#### `GET /api/meetings/{meeting_id}`

Get the merged state (status and runtime stats) for a specific meeting.

**Response:** `200 OK`

| Field | Type | Description |
|-------|------|-------------|
| `meeting_id` | string | The meeting identifier |
| `status` | object | Status information (see StatusInfo structure above) |
| `statistics` | object | Runtime statistics (see Statistics structure below) |
| `users` | array | Array of user info objects (optional, see UserInfo structure) |

**Statistics Structure:**

| Field | Type | Description |
|-------|------|-------------|
| `start_time` | string | ISO 8601 timestamp when meeting started |
| `audio_frames_received` | integer | Total audio frames received |
| `audio_frames_dropped` | integer | Total audio frames dropped (buffer full) |
| `audio_bytes_received` | integer | Total audio bytes received |
| `last_audio_time` | string | ISO 8601 timestamp of last audio frame |
| `video_files_received` | integer | Total video files (playlists + segments) received |
| `video_files_dropped` | integer | Total video files dropped (no subscribers) |
| `video_bytes_received` | integer | Total video bytes received (segments only) |
| `last_video_time` | string | ISO 8601 timestamp of last video file |

**UserInfo Structure:**

| Field | Type | Description |
|-------|------|-------------|
| `id` | integer | User ID (uint64) |
| `name` | string | Display name |
| `audio` | integer | `1` if audio on, `0` if muted |
| `video` | integer | `1` if video on, `0` if off |
| `share` | integer | `1` if sharing screen, `0` otherwise |

**Error Responses:**

| Status Code | Error Field | Description |
|-------------|-------------|-------------|
| `404 Not Found` | `"meeting not found"` | Meeting ID does not exist |
| `500 Internal Server Error` | Error message | Server error during state retrieval |

#### `GET /api/meetings/{meeting_id}/manifest`

Get the manifest describing all recorded media files and event timeline for a meeting.

> **Note on Data Integrity**: The manifest represents what the server **sent** to the WebSocket streams.
> Due to network conditions or client disconnects, this may differ from what the client actually **received**.
> Clients should use the manifest to verify their local recordings against the server's transmission log.

**Response:** `200 OK`

Returns a JSON manifest describing the meeting recording structure:

| Field | Type | Description |
|-------|------|-------------|
| `meeting_id` | string | Meeting identifier |
| `t0_unix_ms` | integer | Meeting start time (Unix milliseconds) |
| `duration_ms` | integer | Total meeting duration (milliseconds) |
| `audio` | array | Audio tracks (see AudioTrack structure) |
| `video` | array | Video tracks (see VideoTrack structure) |
| `events` | array | Timeline events (see ManifestEvent structure) |

**AudioTrack Structure:**

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | Track type: `"mixed"`, `"user"`, or `"share"` |
| `user_id` | integer | User ID (uint64, 0 for mixed audio) |
| `format` | object | Audio format (encoding, sample_rate, channels) |
| `segments` | array | Media segments (see MediaSegment) |

**VideoTrack Structure:**

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | Track type: `"user"` or `"share"` |
| `user_id` | integer | User ID (uint64) |
| `segments` | array | Media segments (see MediaSegment) |

**MediaSegment Structure:**

| Field | Type | Description |
|-------|------|-------------|
| `path` | string | Relative path to media file |
| `start_ms` | integer | Start timestamp (milliseconds since meeting start) |
| `end_ms` | integer | End timestamp (milliseconds since meeting start) |

**ManifestEvent Structure:**

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | Event type: `"meeting_status"` or `"user_event"` |
| `event` | string | Event name (e.g., `"joined"`, `"audio_unmuted"`) |
| `user_id` | string | User ID (optional) |
| `timestamp` | integer | Timestamp (milliseconds since meeting start) |
| `wall_ts` | integer | Wall clock timestamp (Unix milliseconds) |

**Filename Conventions:**

Audio and video files follow this pattern:
- **Audio**: `{user_id}/{type}_{timestamp}.{ext}` (e.g., `.mp3`, `.aac`, `.wav`)
- **Video (HLS)**: `{user_id}/{type}_{timestamp}.m3u8`

Where:
- `{user_id}`: Zoom user ID (uint64, 0 for mixed audio)
- `{type}`: Track type (`"mixed"`, `"user"`, or `"share"`)
- `{timestamp}`: File creation time (Unix milliseconds)
- `{ext}`: Audio encoding extension (`mp3`, `aac`, `wav`)

**Note**: Paths in the manifest are relative. The base directory containing `meeting_id` and `t0` is determined by the storage layer, not embedded in filenames.

**Example Response:**

```json
{
  "meeting_id": "123",
  "t0_unix_ms": 1700000000000,
  "duration_ms": 120000,
  "audio": [
    {
      "type": "mixed",
      "user_id": 0,
      "format": {"encoding": "MP3", "sample_rate": 32000, "channels": 1},
      "segments": [
        {"path": "0/mixed_1700000001000.mp3", "start_ms": 0, "end_ms": 120000}
      ]
    }
  ],
  "video": [
    {
      "type": "user",
      "user_id": 16778243,
      "segments": [
        {"path": "16778243/user_1700000002000.m3u8", "start_ms": 1000, "end_ms": 11000}
      ]
    }
  ],
  "events": [
    {"type": "user_event", "event": "joined", "user_id": "16778243", "timestamp": 500, "wall_ts": 1700000000500}
  ]
}
```

**Error Responses:**

| Status Code | Error Field | Description |
|-------------|-------------|-------------|
| `404 Not Found` | `"meeting not found"` | Meeting ID does not exist or has no manifest data |

#### `GET /health`

Health check endpoint for monitoring.

**Response:** `200 OK`

| Field | Type | Description |
|-------|------|-------------|
| `status` | string | Always `"ok"` |
| `meeting_count` | integer | Number of active meetings |

### WebSocket API

All WebSocket endpoints follow the pattern: `GET /ws/{stream_type}/{meeting_id}`

**Endpoints:**

| Endpoint | Stream Type | Description |
|----------|-------------|-------------|
| `/ws/audio/{meeting_id}` | Audio | Stream real-time encoded audio frames |
| `/ws/events/{meeting_id}` | Events | Receive meeting lifecycle and participant events as JSON |
| `/ws/video/{meeting_id}` | Video | Stream HLS playlist and segment updates |

**Query Parameters (Audio Stream Only):**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `type` | string | - | Comma-separated audio type filter: `mixed`, `one_way`, `share` |
| `user_id` | string | - | Comma-separated user ID filter (uint64 values) |
| `queue_size` | integer | `1000` | Buffer size for pending audio frames (must be positive) |

**Connection Behavior:**

- All WebSocket connections use ping/pong for keepalive
- Server sends ping messages at configured intervals (default: 60 seconds)
- Client should respond with pong messages
- Connection closes if no pong received within read timeout (default: 3 minutes)
- Server automatically closes connections when meeting ends

**End of Meeting Workflow:**

1. **Listen**: Monitor the Events stream for the `meeting_status` event with status `ended`.
2. **Download**: Immediately request the final manifest via `GET /api/meetings/{meeting_id}/manifest`.
3. **Disconnect**: The server will close all WebSocket connections and terminate the worker process shortly after the meeting ends.

#### Audio Stream (`/ws/audio/{meeting_id}`)

Streams encoded audio frames (MP3, AAC, or raw PCM) with filename metadata.

**Message Types:**

**1. Audio Format Message (Text/JSON)**

Sent immediately after WebSocket connection establishment:

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | Always `"audio_format"` |
| `sample_rate` | integer | Audio sample rate in Hz (default: 32000) |
| `channels` | integer | Number of audio channels (default: 1) |
| `sample_format` | string | Audio encoding: `"mp3"`, `"aac"`, or `"s16le"` |

**Example:**
```json
{
  "type": "audio_format",
  "sample_rate": 32000,
  "channels": 1,
  "sample_format": "mp3"
}
```

**2. Binary Audio Frames**

Audio frames are sent as binary WebSocket messages with the following structure:

**Frame Format:**

| Offset | Size | Description |
|--------|------|-------------|
| 0-63 | 64 bytes | Filename header (UTF-8, NUL-padded) |
| 64+ | N bytes | Encoded audio data (MP3/AAC/PCM) |

**Filename Extraction:**

- First 64 bytes contain the filename as UTF-8 string
- Filename is NUL-padded (remaining bytes are `0x00`)
- Actual filename length may be less than 64 bytes
- Filename pattern: `{user_id}/{type}_{timestamp}.{ext}`

**Example Filenames:**
- `"0/mixed_1700000001234.mp3"` - Mixed audio, MP3 encoded
- `"16778243/user_1700000005678.aac"` - User audio, AAC encoded
- `"0/share_1700000009999.mp3"` - Share audio, MP3 encoded

**Audio Types:**

| Value | Name | Description |
|-------|------|-------------|
| `0` | `mixed` | Mixed audio (all participants combined) |
| `1` | `one_way` | One-way audio (individual participant) |
| `2` | `share` | Share audio (screen sharing audio) |

**Audio Encoding:**

The encoding is specified in the `audio_format` message and configured via `AUDIO_ENCODING` environment variable:

| Encoding | Description | File Extension |
|----------|-------------|----------------|
| `MP3` | MP3 encoded audio (default) | `.mp3` |
| `AAC` | AAC-LC encoded audio | `.aac` |
| `S16LE` | Raw PCM (16-bit LE) | `.wav` |

**Performance:**

- **No buffering**: Frames are sent immediately as they're encoded
- **Low latency**: No artificial delay introduced by server
- **Self-contained**: Each frame is a complete, valid audio file segment

#### Events Stream (`/ws/events/{meeting_id}`)

Text-only WebSocket stream delivering meeting lifecycle and participant events as JSON messages.

**Event Structure:**

All events share a common structure:

| Field | Type | Description |
|-------|------|-------------|
| `meeting_id` | string | Meeting identifier |
| `type` | string | Event type: `"meeting_status"` or `"user_event"` |
| `ts` | integer | Timestamp in Unix milliseconds |

**Meeting Status Events:**

When `type` is `"meeting_status"`:

| Field | Type | Description |
|-------|------|-------------|
| `status` | string | Status value (see Meeting States table above) |
| `detail` | integer | Status detail code from Zoom SDK |

**User Events:**

When `type` is `"user_event"`:

| Field | Type | Description |
|-------|------|-------------|
| `event` | string | User event type (see User Event Types table) |
| `user` | object | User information (see UserInfo structure above) |

**User Event Types:**

| Event Type | Description |
|------------|-------------|
| `snapshot` | Initial snapshot of all users in meeting (sent on connection) |
| `joined` | User joined the meeting |
| `left` | User left the meeting |
| `audio_muted` | User muted their microphone |
| `audio_unmuted` | User unmuted their microphone |
| `video_on` | User turned on their video |
| `video_off` | User turned off their video |
| `share_started` | User started screen sharing |
| `share_stopped` | User stopped screen sharing |
| `active_speaking` | User started speaking (VAD detected) **[BETA: Unreliable]** |
| `inactive_speaking` | User stopped speaking (VAD inactive) **[BETA: Unreliable]** |

**Connection Behavior:**

- On connection, server sends snapshot events for all current users in the meeting
- Subsequent events are sent in real-time as they occur
- If meeting has not started, snapshot will be empty array

#### Video Stream (`/ws/video/{meeting_id}`)

WebSocket stream delivering HLS playlist and segment updates. Messages are either text (JSON) for playlists or binary for segments.

**HLS Filename Patterns:**

| File Type | Pattern | Example |
|-----------|---------|---------|
| Playlist | `{prefix}.m3u8` | `live.m3u8` |
| Init File | `{prefix}-init.mp4` | `live-init.mp4` |
| Segment | `{prefix}-part-{NNNNN}.m4s` | `live-part-00042.m4s` |

Where:
- `{prefix}` is the HLS prefix (default: empty string)
- `{NNNNN}` is a 5-digit zero-padded sequence number

**Playlist Messages (Text/JSON):**

| Field | Type | Description |
|-------|------|-------------|
| `filename` | string | Playlist filename (e.g., `"live.m3u8"`) |
| `is_playlist` | boolean | Always `true` |
| `sequence` | integer | Sequence number (uint64) |
| `content` | string | Complete M3U8 playlist content |

**Segment Messages (Binary):**

Binary messages contain video segment data with the following structure:

| Offset | Size | Description |
|--------|------|-------------|
| 0-63 | 64 bytes | Filename header (UTF-8, NUL-padded) |
| 64+ | N bytes | Video segment data (MP4 fragment) |

**Segment Filename Extraction:**

- First 64 bytes contain the filename as UTF-8 string
- Filename is NUL-padded (remaining bytes are `0x00`)
- Actual filename length may be less than 64 bytes
- Remaining bytes after filename header contain the MP4 fragment data

**HLS Configuration:**

| Property | Default | Description |
|----------|---------|-------------|
| Segment Duration | 10 seconds | HLS segment duration |
| Segment Format | fMP4 | Fragmented MP4 |
| Playlist Type | event | Event playlist (appends segments) |
| Video Codec | H.264 | - |
| Audio Codec | AAC | - |
| Video Bitrate | 3000 kbps | - |
| Audio Bitrate | 128 kbps | - |
| Audio Sample Rate | 32000 Hz | - |
| Audio Channels | 1 | Mono |

**Connection Behavior:**

- Playlist messages are sent whenever the playlist is updated (**Overwrite Mode**: The playlist content replaces the previous version)
- Segment messages are sent as segments are generated
- Sequence numbers increment for each segment
- Init file is sent once at the beginning (if configured)

### Error Reference

**HTTP Error Response Format:**

All error responses follow this JSON structure:

| Field | Type | Description |
|-------|------|-------------|
| `error` | string | Error message |
| `code` | integer | HTTP status code |
| `message` | string | Same as `error` field |

**HTTP Status Codes:**

| Code | Meaning | Common Scenarios |
|------|---------|------------------|
| `200 OK` | Success | Successful GET/DELETE operations |
| `202 Accepted` | Accepted | Meeting join request accepted |
| `400 Bad Request` | Client error | Invalid request body, missing required fields, meeting already exists |
| `404 Not Found` | Not found | Meeting ID does not exist |
| `405 Method Not Allowed` | Method not allowed | HTTP method not supported for endpoint |
| `500 Internal Server Error` | Server error | SDK initialization failure, join operation failure, internal errors |

**Common Error Messages:**

| Error Message | Status Code | Description |
|---------------|-------------|-------------|
| `"Invalid request body"` | 400 | Malformed JSON in request body |
| `"Meeting ID is required"` | 400 | Missing `meeting_id` field |
| `"meeting already exists"` | 400 | Attempting to join a meeting that is already active |
| `"meeting not found"` | 404 | Meeting ID does not exist or meeting has ended |
| `"Method not allowed"` | 405 | HTTP method not supported (see Allow header) |

### Configuration

**Environment Variables:**

| Variable | Required | Default | Description |
|----------|----------|---------|-------------|
| `ZOOM_SDK_KEY` | Yes | - | Zoom SDK Key |
| `ZOOM_SDK_SECRET` | Yes | - | Zoom SDK Secret |
| `HTTP_ADDR` | No | `:8080` | HTTP server listen address |
| `LOG_LEVEL` | No | `info` | Log level: `debug`, `info`, `warn`, `error` |
| `AUDIO_SAMPLE_RATE` | No | `32000` | Audio sample rate in Hz |
| `AUDIO_CHANNELS` | No | `1` | Number of audio channels (1 = mono, 2 = stereo) |
| `AUDIO_ENCODING` | No | `S16LE` | Audio encoding: `MP3`, `AAC`, or `S16LE` |
| `AUDIO_BITRATE` | No | `128` | Audio bitrate in kbps (for MP3/AAC) |
| `WEBSOCKET_WRITE_TIMEOUT` | No | `5` | WebSocket write timeout in seconds |
| `WEBSOCKET_READ_TIMEOUT` | No | `180` | WebSocket read timeout in seconds (3 minutes) |
| `WEBSOCKET_PING_INTERVAL` | No | `60` | WebSocket ping interval in seconds |

**Command Line Flags:**

| Flag | Environment Variable | Description |
|------|---------------------|-------------|
| `-http` | `HTTP_ADDR` | HTTP server address |
| `-log-level` | `LOG_LEVEL` | Log level |
| `-sdk-key` | `ZOOM_SDK_KEY` | Zoom SDK key |
| `-sdk-secret` | `ZOOM_SDK_SECRET` | Zoom SDK secret |

**Audio Configuration:**

Configure audio encoding via environment variables:

```bash
# MP3 encoding (recommended)
export AUDIO_ENCODING=MP3
export AUDIO_BITRATE=128  # kbps

# AAC encoding
export AUDIO_ENCODING=AAC
export AUDIO_BITRATE=128  # kbps

# Raw PCM (for backward compatibility)
export AUDIO_ENCODING=S16LE
# Bitrate not applicable for PCM
```

**WebSocket Configuration:**

| Property | Default | Description |
|----------|---------|-------------|
| Write Timeout | 5 seconds | Maximum time to write a message |
| Read Timeout | 3 minutes | Maximum time between pong messages |
| Ping Interval | 60 seconds | Interval between ping messages |
| Default Queue Size | 1000 | Default buffer size for audio frame queue |

### Protocol Examples

**Connecting to Audio Stream:**

1. Establish WebSocket connection to `/ws/audio/{meeting_id}`
2. Receive `audio_format` message (text/JSON)
3. Receive binary audio frames continuously
4. For each binary frame:
   - Extract filename from first 64 bytes (strip NUL padding)
   - Extract encoded audio data from remaining bytes
   - Save or process audio according to encoding type

**Example (JavaScript):**

```javascript
const ws = new WebSocket('ws://localhost:8080/ws/audio/123456789');

ws.onmessage = (event) => {
  if (typeof event.data === 'string') {
    // Audio format message
    const format = JSON.parse(event.data);
    console.log('Format:', format);
  } else {
    // Binary audio frame
    event.data.arrayBuffer().then(buffer => {
      const view = new Uint8Array(buffer);
      
      // Extract filename (first 64 bytes)
      const filenameBytes = view.slice(0, 64);
      const filename = new TextDecoder().decode(filenameBytes).replace(/\0+$/, '');
      
      // Extract audio data (remaining bytes)
      const audioData = view.slice(64);
      
      console.log('Received:', filename, 'Size:', audioData.length);
      // Save or process audioData (MP3/AAC/PCM)
    });
  }
};
```

**Connecting to Events Stream:**

1. Establish WebSocket connection to `/ws/events/{meeting_id}`
2. Receive snapshot events for all current users (if meeting is active)
3. Receive real-time events as they occur
4. Parse JSON messages to extract event type and data
5. Handle meeting status changes and user events accordingly

**Connecting to Video Stream:**

1. Establish WebSocket connection to `/ws/video/{meeting_id}`
2. Receive playlist messages (text/JSON) as playlists are updated
3. Receive segment messages (binary) as segments are generated
4. For binary messages:
   - Extract filename from first 64 bytes (remove NUL padding)
   - Extract segment data from remaining bytes
   - Save or process segment accordingly
5. Use playlist content to understand segment sequence and timing

**Error Handling:**

- Monitor WebSocket connection state
- Handle connection closures gracefully
- Check for error messages in text frames
- Implement reconnection logic with exponential backoff
- Validate meeting ID exists before connecting

**Best Practices:**

- Use appropriate queue sizes based on expected latency
- Process audio frames promptly to avoid buffer overflow
- Save audio/video files immediately to prevent data loss
- Handle meeting end events to clean up resources
- Monitor statistics endpoint for health checks
- Use encoded audio (MP3/AAC) for efficient storage and streaming

## Package Architecture

### Overview

The server uses a unified streaming architecture built on generic bus infrastructure:

```
server/pkg/
├── stream/             # Generic streaming infrastructure
│   ├── bus.go          # Generic bus implementation using Go generics
│   ├── audio.go        # Audio event types and subscriber
│   ├── video.go        # Video file event types
│   ├── events.go       # Event type definitions
│   └── mempool.go      # Memory pooling for efficient buffer management
├── zoombot/            # Zoom bot integration
│   ├── manager.go      # Single-process meeting management (legacy)
│   ├── meeting.go      # Individual meeting instance
│   ├── meeting_types.go # Status and statistics types
│   ├── meeting_handlers.go # Event handlers
│   ├── interface.go    # Manager interface
│   ├── errors.go       # Error definitions
│   └── native/         # CGO bridge to C SDK
│       ├── meeting.go  # Native meeting operations
│       ├── callback.go # Callback routing
│       ├── bytes.go    # Byte utilities
│       └── thread.go   # Thread management
├── manifest/           # Meeting manifest tracking
│   ├── tracker.go      # Manifest tracker implementation
│   ├── types.go        # Manifest data structures
│   ├── meeting_state.go # Per-meeting state management
│   ├── track_state.go  # Audio/video track state
│   ├── naming.go       # Filename parsing utilities
│   └── tracker_test.go # Unit tests
├── server/             # HTTP/WebSocket server
│   ├── http.go         # REST API handlers
│   ├── router.go       # Path parameter routing
│   ├── errors.go       # Error response utilities
│   └── ws/             # WebSocket handlers
│       ├── audio.go    # Audio stream handler
│       ├── video.go    # Video stream handler
│       ├── events.go   # Events stream handler
│       └── common.go   # Shared WebSocket utilities
├── config/             # Configuration management
│   ├── config.go       # Configuration loading and validation
│   └── errors.go       # Configuration errors
└── log/                # Logging utilities
    └── logger.go       # Structured logging
```

**Command Structure:**

```
server/cmd/headless-meeting-bot/
├── main.go             # Entry point (server/worker mode selection)
├── server.go           # Server mode initialization
├── worker.go           # Worker mode implementation
├── manager.go          # ProcessManager implementation
└── streams.go          # GOB stream aggregation from workers
```

### Stream Bus Architecture

All streaming (audio, video, events) is built on a shared generic bus:

**Bus Type Mapping:**

| Stream Type | Go Type | Bus Type |
|-------------|---------|----------|
| Audio | `*stream.AudioEvent` | `stream.AudioBus` |
| Video | `*stream.FileEvent` | `stream.VideoBus` |
| Events | `*stream.Event` | `stream.EventBus` |

**Benefits:**

- **Type safety**: Compile-time type checking via generics
- **Code reuse**: Shared subscriber management, filtering, statistics
- **Consistency**: Uniform API across all streaming types
- **Performance**: Zero-copy publishing, buffered channels, efficient filtering
- **Memory efficiency**: Buffer pooling for binary data

**Subscriber Features:**

| Feature | Description |
|---------|-------------|
| Meeting Filtering | Filter events by meeting ID |
| Custom Filtering | Apply custom filter predicates |
| Buffered Channels | Configurable buffer size per subscriber |
| Statistics | Track delivery success and drops |
| Thread Safety | All operations are thread-safe |

### Protocol Alignment

Audio and video WebSocket protocols share a consistent format:

**Binary Frame Structure (Both Audio & Video):**

```
┌────────────────────────────────────────────┐
│  Filename Header (64 bytes, NUL-padded)   │
├────────────────────────────────────────────┤
│  Media Data (N bytes)                      │
│  - Audio: MP3/AAC/PCM data                 │
│  - Video: MP4 fragment data                │
└────────────────────────────────────────────┘
```

This unified format simplifies client implementation and provides consistent filename metadata for both streams.

### Manifest Tracking System

The manifest tracker builds a comprehensive timeline of all media files and events for each meeting:

**Architecture:**

```
┌──────────────────────────────────────────────────┐
│           Manifest Tracker (InMemoryTracker)     │
│                                                  │
│  ┌────────────────────────────────────────────┐ │
│  │  Per-Meeting State (meetingState)          │ │
│  │                                            │ │
│  │  • t0UnixMs: Meeting start time            │ │
│  │  • audioTracks: map[key]*audioTrackState   │ │
│  │  • videoTracks: map[key]*videoTrackState   │ │
│  │  • events: []ManifestEvent                 │ │
│  │  • duration: Total meeting duration        │ │
│  └────────────────────────────────────────────┘ │
│                                                  │
│  Input Events:                                   │
│  • OnMeetingStart(meetingID, t0)                 │
│  • OnAudioSegment(meetingID, filename, ...)      │
│  • OnVideoSegment(meetingID, filename, ...)      │
│  • OnUserEvent(event)                            │
│  • OnMeetingEnd(meetingID)                       │
│                                                  │
│  Output:                                         │
│  • GetManifest(meetingID) → MeetingManifest      │
└──────────────────────────────────────────────────┘
```

**Key Features:**

| Feature | Description |
|---------|-------------|
| **Timeline Tracking** | All timestamps are relative to t0 (meeting start time) |
| **Segment Management** | Tracks start/end times for each audio/video file |
| **Event Recording** | Captures all meeting and user events with timestamps |
| **Automatic Closure** | Closes segments on mute/share-stop/meeting-end events |
| **Thread Safety** | All operations are protected by mutex |
| **Deep Copy** | GetManifest returns a snapshot, safe for concurrent use |

**Track State Management:**

- **Audio Tracks**: One track per (type, userID) combination
  - Mixed audio (userID=0): Continuous stream from meeting start
  - User audio: Segments created on unmute, closed on mute
  - Share audio: Segments created on share start, closed on share stop

- **Video Tracks**: One track per (type, userID) combination
  - User video: Segments created on video on, closed on video off
  - Share video: Segments created on share start, closed on share stop

**Integration:**

The ProcessManager integrates manifest tracking by:
1. Calling `OnMeetingStart` when first event arrives (sets t0)
2. Calling `OnAudioSegment` when audio files are created
3. Calling `OnVideoSegment` when HLS playlists are created
4. Calling `OnUserEvent` for all user events (join/leave/mute/unmute/share)
5. Calling `OnMeetingEnd` when meeting ends (closes all open segments)


### Multi-Process Architecture

The server uses a **process-per-meeting** architecture to avoid GLib context conflicts in the Zoom SDK:

**Architecture Diagram:**

```
┌─────────────────────────────────────────────┐
│           Main Server Process               │
│                                             │
│  ┌────────────┐      ┌──────────────────┐ │
│  │ HTTP API   │──────│ ProcessManager   │ │
│  └────────────┘      └──────────────────┘ │
│                              │              │
│  ┌────────────┐              │              │
│  │ WebSocket  │              │              │
│  │ Handlers   │              │              │
│  └────────────┘              │              │
│       │                      │              │
│       │    ┌─────────────────┼──────────┐  │
│       └────│ Audio/Video/    │          │  │
│            │ Events Buses    │          │  │
│            └─────────────────┘          │  │
│                                         │  │
│  ┌──────────────────┐                  │  │
│  │ Manifest Tracker │◄─────────────────┘  │
│  └──────────────────┘                     │
└─────────────────────────────────────────┼──┘
                                          │
              ┌───────────────────────────┴───────────────┐
              │                                           │
              ▼                                           ▼
    ┌─────────────────┐                        ┌─────────────────┐
    │ Worker Process  │                        │ Worker Process  │
    │  (Meeting 1)    │                        │  (Meeting 2)    │
    │                 │                        │                 │
    │ ┌─────────────┐ │                        │ ┌─────────────┐ │
    │ │ Zoom SDK    │ │                        │ │ Zoom SDK    │ │
    │ │ Instance    │ │                        │ │ Instance    │ │
    │ └─────────────┘ │                        │ └─────────────┘ │
    │                 │                        │                 │
    │ HTTP Server:    │                        │ HTTP Server:    │
    │ • /health       │                        │ • /health       │
    │ • /state        │                        │ • /state        │
    │ • /audio        │                        │ • /audio        │
    │ • /events       │                        │ • /events       │
    │ • /video        │                        │ • /video        │
    │ • /shutdown     │                        │ • /shutdown     │
    └─────────────────┘                        └─────────────────┘
            │                                           │
            └──────────GOB Streaming (HTTP)────────────┘
                    (to main server buses)
```

**Key Components:**

| Component | Responsibility |
|-----------|----------------|
| Main Server | Handles API requests, manages WebSocket connections, aggregates streams, tracks manifest |
| ProcessManager | Spawns/manages worker processes, aggregates GOB streams, maintains meeting state |
| Worker Processes | Isolated Zoom SDK instances, one per meeting, streams data via HTTP |
| Stream Aggregation | Workers stream GOB-encoded data to main server via HTTP endpoints |
| Bus Distribution | Main server publishes to buses, WebSocket clients subscribe |
| Manifest Tracker | Tracks audio/video segments and events, builds meeting manifest |

**Communication Protocol:**

Workers communicate with the main server using **GOB-encoded HTTP streams**:

1. **Worker Startup**: ProcessManager spawns worker with unique port
2. **Health Check**: Main server polls `/health` endpoint until worker is ready
3. **Stream Aggregation**: Main server subscribes to worker's `/audio`, `/events`, `/video` endpoints
4. **GOB Decoding**: Main server decodes GOB streams and publishes to buses
5. **Manifest Tracking**: Events and file notifications update the manifest tracker
6. **State Queries**: Main server queries `/state` endpoint for meeting status/statistics
7. **Shutdown**: Main server calls `/shutdown` endpoint to gracefully stop worker

**Benefits:**

- **Isolation**: Each meeting runs in separate process, preventing SDK conflicts
- **Scalability**: Can run multiple meetings concurrently
- **Reliability**: Failure in one meeting doesn't affect others
- **Resource Management**: Better control over memory and CPU per meeting
- **Clean Shutdown**: Workers can be terminated independently

### Building

**Build Commands:**

| Command | Description |
|---------|-------------|
| `bash build.sh` | Build the unified binary (contains both server and worker modes) |
| `./headless-meeting-bot server` | Run in server mode |
| `./headless-meeting-bot worker -config '{...}'` | Run in worker mode (used internally by server) |

**Binary Modes:**

| Mode | Purpose |
|------|----|
| `server` | Main server process that handles API and WebSocket connections |
| `worker` | Worker process that runs Zoom SDK for a single meeting |
