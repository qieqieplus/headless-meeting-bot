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
  "join_token": "optional_join_token_for_recording_authorization"
}
```

**Fields:**
- `meeting_id` (required): The meeting ID to join
- `password` (optional): Meeting password if required
- `display_name` (optional): Display name in the meeting (default: "Recording Bot")
- `join_token` (optional): Join token for automatic recording authorization

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
    "join_token": "your_token_here"
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
    "status": "MEETING_STATUS_CONNECTING"
  }
]
```

#### `GET /api/meetings/{meeting_id}`

Get the status of a specific meeting.

**Response:** `200 OK`

```json
{
  "meeting_id": "1234567890",
  "status": "MEETING_STATUS_INMEETING",
  "error": null,
  "stats": {
    "cpu": "0.5%",
    "memory": "100MB"
  }
}
```

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

**Query Parameters:**
- `type` (optional): Audio type filter (`mixed`, `one_way`, `share`)
- `user_id` (optional): Filter by specific user IDs
- `queue_size` (optional): Audio queue size (default: 1000)

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
