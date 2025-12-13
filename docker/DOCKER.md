# Docker Deployment Guide

This document provides comprehensive instructions for building and deploying the headless meeting bot using Docker.

## Overview

The Docker setup provides two distinct images:

### Build Image (`Dockerfile.build`)
- **Purpose**: Compiles the Go application and C libraries
- **Base**: `ubuntu:22.04` with build dependencies
- **Includes**: FFmpeg development libraries (libavcodec-dev, libavformat-dev, libx264-dev, etc.) for CPU-only video encoding
- **Output**: Built binaries and libraries
- **Use case**: CI/CD pipelines, reproducible builds

### Deploy Image (`Dockerfile`)
- **Purpose**: Runs the pre-built application
- **Base**: `ubuntu:22.04` with runtime dependencies only
- **Includes**: All required C libraries (Zoom SDK, Qt, OpenSSL, FFmpeg runtime libraries, etc.)
- **Features**: Non-root user, health checks, resource limits, CPU-based video encoding
- **Use case**: Production deployment

## Prerequisites

1. **Docker installed** on your system
2. **Zoom SDK credentials** (ZOOM_SDK_KEY and ZOOM_SDK_SECRET)

## Quick Start

### Option 1: Build Everything in Docker (Recommended)

```bash
# 1. Build the compilation image and extract artifacts
./docker-build.sh build

# 2. Extract built artifacts (if needed)
docker run --rm -v $(pwd):/output headless-meeting-bot-build:latest cp /output/headless-meeting-bot-build.tar.gz /output/

# 3. Build the deploy image
./docker-build.sh deploy

# 4. Run the application
docker run -d --name meeting-bot \
  -p 8080:8080 \
  -e ZOOM_SDK_KEY="your_key" \
  -e ZOOM_SDK_SECRET="your_secret" \
  headless-meeting-bot:latest
```

### Option 2: Build Locally, Deploy in Docker

```bash
# 1. Build locally first
cd ../../src && mkdir -p build && cd build && cmake .. && make
cd ../../server && ./build.sh

# 2. Build deploy image
./docker-build.sh deploy

# 3. Run the application
docker run -d --name meeting-bot \
  -p 8080:8080 \
  -e ZOOM_SDK_KEY="your_key" \
  -e ZOOM_SDK_SECRET="your_secret" \
  headless-meeting-bot:latest
```

### 2. Run with Docker Compose

```bash
# Copy and edit environment file
cp docker/env.example docker/.env
# Edit docker/.env with your Zoom SDK credentials

# Start the service (from project root)
docker-compose -f docker/docker-compose.yml up -d

# View logs
docker-compose -f docker/docker-compose.yml logs -f
```

### 3. Manual Docker Run

```bash
docker run -d --name meeting-bot \
  -p 8080:8080 \
  -e ZOOM_SDK_KEY="your_sdk_key" \
  -e ZOOM_SDK_SECRET="your_sdk_secret" \
  headless-meeting-bot:latest
```

## Docker Features

### Simple Runtime Image
- **Single stage**: Uses `ubuntu:22.04` with only runtime dependencies
- **Pre-built binaries**: Expects Go application and C libraries to be built locally

### Included Libraries
- **Core**: libc, libstdc++, libgcc, libatomic
- **SSL/TLS**: OpenSSL libraries
- **GLib**: glib, libglib
- **X11**: libx11, libxcb, libxfixes
- **Graphics**: mesa-gbm, libdrm, libgbm
- **Audio**: pulseaudio
- **Video Encoding**: FFmpeg runtime libraries (libavcodec, libavformat, libavutil, libswscale, libswresample, libavfilter), libx264 (CPU-only encoding)
- **System**: dbus
- **Compression**: zlib
- **Security**: libasan6

### Security Features
- Non-root user (appuser:appgroup)
- No new privileges
- Resource limits (1.5GB RAM, 2 CPUs for video encoding)
- Health checks

### Video Streaming Architecture
The container does **not** host HLS files on disk. Video segments are generated in-memory using FFmpeg and streamed to clients in real-time. Clients are responsible for:
- Consuming video streams via WebSocket or HTTP endpoints
- Saving/uploading segments to their own storage
- Reconstructing HLS playlists and segments from the streamed data

**Video Streaming Endpoints:**
- **WebSocket**: `ws://<host>:8080/ws/video/{meeting_id}`
  - Playlists (`.m3u8`) are sent as JSON text messages with `is_playlist: true` and `content` field
  - Segments (`.m4s`, `init.mp4`) are sent as binary messages with a 64-byte filename header followed by the segment data
- **HTTP NDJSON Stream**: Available via worker endpoints (if configured)
  - Each line is a JSON object containing `filename`, `is_playlist`, `sequence`, and either `content` (for playlists) or `data` (base64-encoded for segments)

**Example Client Flow:**
1. Connect to WebSocket endpoint with meeting ID
2. Receive playlist updates as JSON text messages
3. Receive segment files as binary messages (64-byte filename header + data)
4. Save segments to your storage system
5. Reconstruct and serve HLS playlists to your video players

**Encoding Configuration:**
- **Codec**: H.264 (x264, CPU-only)
- **Container**: HLS fMP4 (segmented MP4)
- **Audio**: AAC
- No GPU acceleration (NVENC/VAAPI) - pure CPU encoding

## Environment Variables

| Variable | Required | Default | Description |
|----------|----------|---------|-------------|
| `ZOOM_SDK_KEY` | Yes | - | Zoom SDK key |
| `ZOOM_SDK_SECRET` | Yes | - | Zoom SDK secret |
| `HTTP_ADDR` | No | `:8080` | Server listen address |
| `LOG_LEVEL` | No | `info` | Log level (debug, info, warn, error) |

## Troubleshooting

### Permission Issues
If you get permission denied errors:
```bash
# Add user to docker group
sudo usermod -aG docker $USER
# Log out and log back in
```

### Missing Libraries
If the container fails to start due to missing libraries:
1. Ensure C SDK is built: `cd ../../src/build && make`
2. Check library paths in Dockerfile
3. Verify all required .so files exist

### Build Failures
If Docker build fails:
1. Run validation: `./validate-dockerfile.sh`
2. Check all required files exist
3. Ensure sufficient disk space
4. Check Docker daemon is running

### Runtime Issues
If the container starts but doesn't work:
1. Check logs: `docker logs meeting-bot`
2. Verify environment variables are set
3. Check port 8080 is not in use
4. Ensure Zoom SDK credentials are valid

## Production Deployment

### Docker Compose with Environment File
```yaml
version: '3.8'
services:
  headless-meeting-bot:
    image: headless-meeting-bot:latest
    environment:
      - ZOOM_SDK_KEY=${ZOOM_SDK_KEY}
      - ZOOM_SDK_SECRET=${ZOOM_SDK_SECRET}
    ports:
      - "8080:8080"
    restart: unless-stopped
    deploy:
      resources:
        limits:
          memory: 1536M
          cpus: '2.0'
```

### Kubernetes Deployment
```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: headless-meeting-bot
spec:
  replicas: 1
  selector:
    matchLabels:
      app: headless-meeting-bot
  template:
    metadata:
      labels:
        app: headless-meeting-bot
    spec:
      containers:
      - name: headless-meeting-bot
        image: headless-meeting-bot:latest
        ports:
        - containerPort: 8080
        env:
        - name: ZOOM_SDK_KEY
          valueFrom:
            secretKeyRef:
              name: zoom-credentials
              key: sdk-key
        - name: ZOOM_SDK_SECRET
          valueFrom:
            secretKeyRef:
              name: zoom-credentials
              key: sdk-secret
        resources:
          limits:
            memory: 1536Mi
            cpu: '2'
          requests:
            memory: 512Mi
            cpu: '1'
```

## Monitoring

### Health Checks
The container includes a health check that verifies the HTTP endpoint:
```bash
# Check container health
docker ps
# Look for "healthy" status

# Manual health check
curl http://localhost:8080/health
```

### Logs
```bash
# View logs
docker logs meeting-bot

# Follow logs
docker logs -f meeting-bot

# With Docker Compose
docker-compose -f docker/docker-compose.yml logs -f
```

### Resource Usage
```bash
# Check resource usage
docker stats meeting-bot

# Check container details
docker inspect meeting-bot
```

## Maintenance

### Updating the Image
```bash
# Rebuild with latest changes
./docker-build.sh

# Update running container
docker-compose -f docker/docker-compose.yml down
docker-compose -f docker/docker-compose.yml up -d
```

### Video Stream Management
Since video segments are streamed in-memory and not persisted to disk, clients must handle storage:

```bash
# Example: Save WebSocket video stream to files
# Use the test_video_recorder.py script or implement your own client
python server/test_video_recorder.py --meeting-id <id> --output-dir ./recordings
```

The container streams HLS segments in real-time. For persistent storage, implement a client that:
1. Connects to the WebSocket endpoint
2. Receives and saves segments as they arrive
3. Maintains the HLS playlist structure
4. Uploads to your storage backend (S3, GCS, etc.)

### Cleanup
```bash
# Remove stopped containers
docker container prune

# Remove unused images
docker image prune

# Remove unused volumes
docker volume prune
```
