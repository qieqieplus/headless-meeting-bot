# Headless Meeting Bot - Go Server

For setup and deployment instructions, see the [main README](../README.md).

## API Documentation

### Join Token Feature

The `join_token` parameter enables automatic recording authorization when joining meetings. This is useful for scenarios where you need to automatically start recording without manual intervention:

- **Purpose**: Allows the bot to automatically start recording when joining a meeting
- **Usage**: Include the `join_token` field in your POST request to `/api/meetings`
- **Note**: The join token is obtained from Zoom's API or SDK configuration for recording-enabled applications

### REST API

#### `POST /api/meetings`

Join a meeting.

**Request Body:**

```json
{
  "meeting_id": "1234567890",
  "password": "password",
  "display_name": "Recording Bot",
  "join_token": "optional_join_token_for_recording_authorization",
  "enable_audio": true,
  "enable_video": false
}
```

**Fields:**
- `meeting_id` (required): The meeting ID to join
- `password` (optional): Meeting password if required
- `display_name` (optional): Display name in the meeting (default: "Recording Bot")
- `join_token` (optional): Join token for automatic recording authorization
- `enable_audio` (optional): Request audio capture; defaults to `true` if both audio and video are omitted or `false`
- `enable_video` (optional): Request HLS video capture; defaults to `false`

If both `enable_audio` and `enable_video` are `false` or omitted, the server will implicitly enable audio recording to ensure the bot joins with recording privileges.

**Response:** `202 Accepted`

```json
{
  "status": "joining"
}
```

**Example with join token:**

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

```json
{
  "status": "left"
}
```

#### `GET /api/meetings`

List all active meetings.

**Response:** `200 OK`

```json
[
  {
    "meeting_id": "1234567890",
    "status": "in_meeting"
  }
]
```

#### `GET /api/meetings/{meeting_id}`

Get the merged state (status and runtime stats) for a specific meeting.

**Response:** `200 OK`

```json
{
  "meeting_id": "1234567890",
  "status": {
    "state": "in_meeting",
    "detail": 0
  },
  "stats": {
    "start_time": "2025-11-06T12:34:56Z",
    "audio_frames_received": 12500,
    "audio_frames_dropped": 2,
    "audio_bytes_received": 1024000,
    "last_audio_frame_time": "2025-11-06T12:36:01Z",
    "video_files_received": 128,
    "video_files_dropped": 1,
    "video_bytes_received": 5242880,
    "last_video_file_time": "2025-11-06T12:35:45Z"
  }
}
```

If the Zoom SDK reported a recent error, the `status.error` field is populated with the message. When no error occurred it is omitted.

#### `GET /health`

Health check endpoint for monitoring.

**Response:** `200 OK`

```json
{
  "status": "ok",
  "meeting_count": 2
}
```

### WebSocket API
- `GET /ws/audio/{meeting_id}` - Stream real-time audio from a meeting
- `GET /ws/events/{meeting_id}` - Receive meeting events as JSON
- `GET /ws/video/{meeting_id}` - Stream HLS playlist and segment updates

**Query Parameters:**
- `type` (optional): Audio type filter (`mixed`, `one_way`, `share`)
- `user_id` (optional): Filter by specific user IDs
- `queue_size` (optional): Positive integer buffer size for pending audio frames (default: 1000)

**Message Types:**

#### Text Messages (JSON)

**1. Audio Format Message** (`audio_format`)
Sent immediately after WebSocket connection establishment:
```json
{
  "type": "audio_format",
  "sample_rate": 32000,
  "channels": 1,
  "sample_format": "s16le"
}
```

**2. Heartbeat Message** (`heartbeat`)
Sent periodically to keep connection alive:
```json
{
  "type": "heartbeat",
  "timestamp": 1640995200000
}
```

**3. Error Message** (`error`)
Sent when an error occurs:
```json
{
  "type": "error",
  "error": "Meeting not found",
  "code": 404
}
```

#### Binary Messages (Audio Frames)

Audio frames are sent as binary WebSocket messages with the following structure:

**Frame Header (16 bytes):**
```
Offset  Size    Type     Description
0       8       uint64   Audio Type (little-endian)
8       8       uint64   User ID (little-endian)
16      N       bytes    PCM Audio Data (S16LE format)
```

**Audio Types:**
- `0`: Mixed audio (all participants combined)
- `1`: One-way audio (individual participant)
- `2`: Share audio (screen sharing audio)

**Audio Data Format:**
- **Sample Rate**: 32kHz (configurable)
- **Channels**: 1 (mono)
- **Bit Depth**: 16-bit signed integer
- **Endianness**: Little-endian
- **Encoding**: Linear PCM (uncompressed)

**Example Frame Structure:**
```
[Audio Type: 8 bytes][User ID: 8 bytes][PCM Data: N bytes]
0x0000000000000000   0x0000000000000001   [audio samples...]
(mixed audio)        (user ID 1)          (S16LE stereo)
```

#### Events Stream
- Text-only WebSocket stream of meeting lifecycle and participant events
- Each message is JSON matching the event payload published by the bus
- Use to track meeting state changes, participants joining/leaving, recording status

**Sample Message:**
```json
{
  "type": "user_joined",
  "meeting_id": "1234567890",
  "user_id": "987654321",
  "display_name": "Guest",
  "timestamp": 1640995200000
}
```

#### Video Stream
- Text WebSocket stream delivering both playlist (`.m3u8`) and segment (`.ts`) updates
- Messages are JSON envelopes with metadata and either base64-encoded binary or playlist content
- Useful for recording the shared HLS output or mirroring to a CDN

**Sample Playlist Message:**
```json
{
  "filename": "meeting-123/live.m3u8",
  "is_playlist": true,
  "sequence": 42,
  "content": "#EXTM3U\n#EXT-X-VERSION:3\n..."
}
```

**Sample Segment Message:**
```json
{
  "filename": "meeting-123/segment-0042.ts",
  "is_playlist": false,
  "sequence": 42,
  "data": "<base64 bytes>"
}
```

## Package Architecture

### Overview

The server uses a unified streaming architecture built on generic bus infrastructure:

```
server/pkg/
├── media/              # Media streaming packages
│   ├── audio/          # PCM audio frame streaming
│   │   ├── bus.go      # Audio bus (wraps stream.Bus[*AudioFrame])
│   │   ├── frame.go    # AudioFrame definition and encoding
│   │   └── pool.go     # Frame pooling for efficiency
│   └── video/          # HLS video streaming
│       ├── bus.go      # Video bus (wraps stream.Bus[*FileEvent])
│       └── event.go    # FileEvent definition (playlists/segments)
├── stream/             # Generic streaming infrastructure
│   └── bus.go          # Generic bus implementation using Go generics
├── events/             # Meeting and user events
│   ├── bus.go          # Events bus (wraps stream.Bus[*Event])
│   └── events.go       # Event type definitions
├── zoombot/            # Zoom bot integration
│   ├── bridge.go       # CGO bridge to C SDK
│   ├── manager.go      # Multi-meeting management
│   └── meeting.go      # Individual meeting instance
├── server/             # HTTP/WebSocket server
│   ├── http.go         # REST API handlers
│   ├── ws.go           # WebSocket handlers
│   └── protocol.go     # Wire protocol definitions
├── config/             # Configuration management
└── log/                # Logging utilities
```

### Stream Bus Architecture

All streaming (audio, video, events) is built on a shared generic bus:

```go
// Generic bus supports any data type
type Bus[T any] struct {
    subscribers map[string]*Subscriber[T]
    // ... shared logic for pub/sub, filtering, stats
}

// Audio uses stream.Bus[*AudioFrame]
audioBus := audio.NewBus()

// Video uses stream.Bus[*FileEvent]
videoBus := video.NewBus()

// Events use stream.Bus[*Event]
eventsBus := events.NewBus()
```

**Benefits:**
- **Type safety**: Compile-time type checking via generics
- **Code reuse**: Shared subscriber management, filtering, statistics
- **Consistency**: Uniform API across all streaming types
- **Performance**: Zero-copy publishing, buffered channels, efficient filtering

### Subscriber API

All buses share the same subscriber interface:

```go
// Subscribe to audio
subscriber := audio.NewSubscriber("client-1", 1000)
subscriber.SetMeetingFilter("meeting-123")
subscriber.SetAudioTypeFilter([]audio.AudioType{audio.AudioTypeMixed})
audioBus.Subscribe(subscriber)

// Consume data
for frame := range subscriber.Channel() {
    // Process frame
}

// Cleanup
audioBus.Unsubscribe("client-1")
```

### Multi-Process Architecture

The server uses a process-per-meeting architecture to avoid GLib context conflicts:

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
    │ Streams:        │                        │ Streams:        │
    │ • Audio frames  │                        │ • Audio frames  │
    │ • Events        │                        │ • Events        │
    │ • HLS files     │                        │ • HLS files     │
    └─────────────────┘                        └─────────────────┘
            │                                           │
            └──────────HTTP Streaming (NDJSON)─────────┘
                    (to main server buses)
```

**Key Components:**

1. **Main Server**: Handles API requests, manages WebSocket connections, aggregates streams
2. **Worker Processes**: Isolated Zoom SDK instances, one per meeting
3. **Stream Aggregation**: Workers stream data to main server via HTTP (newline-delimited JSON)
4. **Bus Distribution**: Main server publishes to buses, WebSocket clients subscribe

### Building

```bash
# Build the unified binary (contains both server and worker modes)
bash build.sh

# Run server
./headless-meeting-bot server

# Worker mode (used internally by server)
./headless-meeting-bot worker -config '{...}'
```
