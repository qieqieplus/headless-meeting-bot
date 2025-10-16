# Deploy Docker image for running pre-built binaries (Ubuntu Jammy)
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive \
    TZ=Asia/Shanghai

RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && \
    echo $TZ > /etc/timezone && \
    sed -i 's/archive.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list && \
    sed -i 's/security.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list

# Install runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    libasan6 \
    libatomic1 \
    libgbm1 \
    libglib2.0-0 \
    libxfixes3 \
    libdbus-1-3 \
    libgl1 \
    libx11-xcb1 \
    libxcb-xfixes0 \
    libxcb-shape0 \
    libxcb-shm0 \
    libxcb-randr0 \
    libxcb-image0 \
    libxcb-keysyms1 \
    libxcb-xtest0 \
    pciutils \
    net-tools \
    dbus \
    dbus-x11 \
    xvfb \
    wget \
    pulseaudio \
    && rm -rf /var/lib/apt/lists/*

 # Create non-root user
RUN groupadd -g 1000 appgroup && \
    useradd -u 1000 -g appgroup -s /bin/bash -m appuser

# Set working directory
WORKDIR /app

# Copy the entrypoint script first (needs to be executable by appuser)
COPY server/docker/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

# Copy the pre-built binary
COPY --chown=appuser:appgroup server/headless-meeting-bot .

# Copy required libraries from host (these will be copied from the build context)
# Note: These paths assume the libraries are available in the build context
COPY --chown=appuser:appgroup src/build/libzoomsdk_c.so* ./
COPY --chown=appuser:appgroup lib/zoomsdk/lib* ./

# Copy Qt libraries (minimal set required)
COPY --chown=appuser:appgroup lib/zoomsdk/qt_libs/Qt/lib/lib* ./

# Create directories for recordings and logs
RUN mkdir -p /app/recordings /app/logs && \
    chown -R appuser:appgroup /app

# Set environment variables
ENV LD_LIBRARY_PATH=/app
ENV HTTP_ADDR=:8080
ENV LOG_LEVEL=info
ENV DISPLAY=:99

# Expose port
EXPOSE 8080

# Set entrypoint
ENTRYPOINT ["/entrypoint.sh"]

# Switch to non-root user
USER appuser

# Health check
HEALTHCHECK --interval=60s --timeout=10s --start-period=10s --retries=3 \
    CMD wget --no-verbose --tries=1 --spider http://localhost:8080/health || exit 1

# Default command
CMD ["./headless-meeting-bot", "server"]
