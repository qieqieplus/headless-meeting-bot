# Headless Meeting Bot - Go Server

This is the Go server component of the headless Zoom meeting bot that provides real-time audio streaming from Zoom meetings via WebSocket.

## Scripts

### `build.sh` - Build the Go application
Builds the Go server binary from source code.

```bash
./build.sh
```

**What it does:**
- Compiles the Go application to `headless-meeting-bot` binary
- Shows build information and file details

### `run.sh` - Run the server
Runs the pre-built server binary with proper environment setup.

```bash
./run.sh
```

**What it does:**
- Sets up environment variables (LD_LIBRARY_PATH, etc.)
- Validates that the binary exists
- Validates required credentials
- Starts the server

## Environment Variables

### Required
- `ZOOM_SDK_KEY` - Your Zoom SDK key
- `ZOOM_SDK_SECRET` - Your Zoom SDK secret

### Optional
- `HTTP_ADDR` - Server listen address (default: `:8080`)
- `LOG_LEVEL` - Log level (default: `info`)

## Usage

1. **First build:**
   ```bash
   ./build.sh
   ```

2. **Set credentials:**
   ```bash
   export ZOOM_SDK_KEY="your_sdk_key"
   export ZOOM_SDK_SECRET="your_sdk_secret"
   ```

3. **Run the server:**
   ```bash
   ./run.sh
   ```

## API Endpoints

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
  "display_name": "My Bot",
  "join_token": "optional_join_token_for_recording_authorization"
}
```

**Fields:**
- `meeting_id` (required): The meeting ID to join
- `password` (optional): Meeting password if required
- `display_name` (optional): Display name in the meeting (default: "Zoom Bot")
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
  "sample_rate": 48000,
  "channels": 2,
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
- **Sample Rate**: 48kHz (configurable)
- **Channels**: 2 (stereo)
- **Bit Depth**: 16-bit signed integer
- **Endianness**: Little-endian
- **Encoding**: Linear PCM (uncompressed)

**Example Frame Structure:**
```
[Audio Type: 8 bytes][User ID: 8 bytes][PCM Data: N bytes]
0x0000000000000000   0x0000000000000001   [audio samples...]
(mixed audio)        (user ID 1)          (S16LE stereo)
```


**Runtime Environment:**
- `LD_LIBRARY_PATH`: Runtime library search paths
- Automatically set by `run.sh` for proper library loading

## Docker Deployment

The Docker-related files are organized in the `docker/` directory for better project structure:

- `docker/Dockerfile.build` - Build image for compilation
- `docker/Dockerfile` - Deploy image for runtime
- `docker/docker-compose.yml` - Deploy container orchestration
- `docker/docker-compose.build.yml` - Build container orchestration
- `docker/docker-build.sh` - Build script supporting both modes
- `docker/env.example` - Environment variables template
- `docker/DOCKER.md` - Comprehensive deployment guide

### Quick Start with Docker

1. **Build everything in Docker (recommended):**
   ```bash
   # Build compilation image
   ./docker/docker-build.sh build
   
   # Build deploy image
   ./docker/docker-build.sh deploy
   ```
   
   **Or build locally first:**
   ```bash
   # Build locally
   ./build.sh
   
   # Build deploy image only
   ./docker/docker-build.sh deploy
   ```

2. **Set up environment variables:**
   ```bash
   cp docker/env.example .env
   # Edit .env with your Zoom SDK credentials
   ```

3. **Run with Docker Compose:**
   ```bash
   docker-compose -f docker/docker-compose.yml up -d
   ```

4. **View logs:**
   ```bash
   docker-compose -f docker/docker-compose.yml logs -f
   ```

### Manual Docker Run

```bash
# Build the deploy image
./docker/docker-build.sh deploy

# Run the container
docker run -d --name meeting-bot \
  -p 8080:8080 \
  -e ZOOM_SDK_KEY="your_sdk_key" \
  -e ZOOM_SDK_SECRET="your_sdk_secret" \
  -v $(pwd)/recordings:/app/recordings \
  headless-meeting-bot:latest
```

### Docker Features

- **Minimal Alpine Linux base image** for small footprint
- **All required C libraries included** (Zoom SDK, Qt, OpenSSL, etc.)
- **Non-root user** for security
- **Health checks** for container monitoring
- **Volume mounts** for persistent recordings
- **Resource limits** to prevent excessive usage
- **Separate build and deploy images** for clean separation of concerns
- **Build image**: Compiles everything in Docker
- **Deploy image**: Minimal runtime with pre-built binaries

### Docker Environment Variables

- `ZOOM_SDK_KEY` (required): Your Zoom SDK key
- `ZOOM_SDK_SECRET` (required): Your Zoom SDK secret
- `HTTP_ADDR` (optional): Server listen address (default: `:8080`)
- `LOG_LEVEL` (optional): Log level (default: `info`)

### Docker Volumes

- `/app/recordings`: Directory for meeting recordings
- `/app/logs`: Directory for application logs (optional)
