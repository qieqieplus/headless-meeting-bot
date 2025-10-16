# Docker Deployment Guide

This document provides comprehensive instructions for building and deploying the headless meeting bot using Docker.

## Overview

The Docker setup provides two distinct images:

### Build Image (`Dockerfile.build`)
- **Purpose**: Compiles the Go application and C libraries
- **Base**: `golang:1.22-alpine` with build dependencies
- **Output**: Built binaries and libraries in a tarball
- **Use case**: CI/CD pipelines, reproducible builds

### Deploy Image (`Dockerfile`)
- **Purpose**: Runs the pre-built application
- **Base**: `alpine:3.19` with runtime dependencies only
- **Includes**: All required C libraries (Zoom SDK, Qt, OpenSSL, etc.)
- **Features**: Non-root user, health checks, resource limits, volume mounts
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
cp env.example .env
# Edit .env with your Zoom SDK credentials

# Start the service
docker-compose up -d

# View logs
docker-compose logs -f
```

### 3. Manual Docker Run

```bash
docker run -d --name meeting-bot \
  -p 8080:8080 \
  -e ZOOM_SDK_KEY="your_sdk_key" \
  -e ZOOM_SDK_SECRET="your_sdk_secret" \
  -v $(pwd)/recordings:/app/recordings \
  headless-meeting-bot:latest
```

## Docker Features

### Simple Runtime Image
- **Single stage**: Uses minimal `alpine:3.19` with only runtime dependencies
- **Pre-built binaries**: Expects Go application and C libraries to be built locally

### Included Libraries
- **Core**: libc, libstdc++, libgcc, libatomic
- **SSL/TLS**: libcrypto3, openssl
- **GLib**: glib, libglib
- **X11**: libx11, libxcb, libxfixes
- **Graphics**: mesa-gbm, mesa-dri-gallium, libdrm
- **Audio**: alsa-lib, pulseaudio
- **System**: dbus, systemd-libs
- **Compression**: zlib, libbz2, liblz4, liblzma, zstd
- **Security**: libcap, libgcrypt, libgpg-error
- **Network**: krb5-libs, libgssapi
- **ICU**: icu-libs

### Security Features
- Non-root user (appuser:appgroup)
- No new privileges
- Resource limits (512MB RAM, 1 CPU)
- Health checks

### Volume Mounts
- `/app/recordings`: Meeting recordings
- `/app/logs`: Application logs (optional)

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
    volumes:
      - ./recordings:/app/recordings
      - ./logs:/app/logs
    ports:
      - "8080:8080"
    restart: unless-stopped
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
        volumeMounts:
        - name: recordings
          mountPath: /app/recordings
      volumes:
      - name: recordings
        persistentVolumeClaim:
          claimName: recordings-pvc
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
docker-compose logs -f
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
docker-compose down
docker-compose up -d
```

### Backup Recordings
```bash
# Copy recordings from container
docker cp meeting-bot:/app/recordings ./backup-recordings

# Or use volume mounts for automatic persistence
```

### Cleanup
```bash
# Remove stopped containers
docker container prune

# Remove unused images
docker image prune

# Remove unused volumes
docker volume prune
```
