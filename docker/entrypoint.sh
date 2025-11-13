#!/bin/bash
set -e

echo "=== Starting container services ==="

# Create runtime directory for DBus with proper permissions
mkdir -p /tmp/dbus-runtime
chmod 700 /tmp/dbus-runtime

# Start DBus session bus
echo "Starting DBus session bus..."
export DBUS_SESSION_BUS_ADDRESS="unix:path=/tmp/dbus-runtime/bus"
dbus-daemon --session \
    --address="$DBUS_SESSION_BUS_ADDRESS" \
    --nofork \
    --nopidfile \
    --syslog-only &
DBUS_PID=$!
echo "DBus started with PID: $DBUS_PID"
sleep 2

# Verify DBus is running
if ! kill -0 $DBUS_PID 2>/dev/null; then
    echo "ERROR: DBus failed to start"
    ps aux | grep dbus || true
    exit 1
fi
echo "✓ DBus is running"

# Start PulseAudio with dummy sink (for audio device emulation)
echo "Starting PulseAudio with dummy sink..."
pulseaudio --start \
    --log-target=syslog \
    --system=false \
    --daemonize=true \
    --high-priority=false \
    --exit-idle-time=-1 \
    --load="module-null-sink sink_name=dummy" \
    --load="module-native-protocol-unix"
sleep 2

# Verify PulseAudio is running
if ! pgrep -x pulseaudio > /dev/null; then
    echo "ERROR: PulseAudio failed to start"
    ps aux | grep pulseaudio || true
    exit 1
fi
echo "✓ PulseAudio is running"

# Start Xvfb (virtual X11 display)
echo "Starting Xvfb on display ${DISPLAY}..."
Xvfb ${DISPLAY} \
    -screen 0 1024x768x24 \
    -ac \
    +extension GLX \
    +render \
    -noreset \
    -nolisten tcp &
XVFB_PID=$!
echo "Xvfb started with PID: $XVFB_PID"
sleep 2

# Verify Xvfb is running
if ! kill -0 $XVFB_PID 2>/dev/null; then
    echo "ERROR: Xvfb failed to start"
    ps aux | grep Xvfb || true
    exit 1
fi
echo "✓ Xvfb is running"

echo "All services started successfully"
echo "==================================="
echo "DISPLAY=$DISPLAY"
echo "DBUS_SESSION_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS"
echo "PulseAudio: dummy sink active"
echo "==================================="
echo

# Trap SIGTERM and SIGINT to cleanup
cleanup() {
    echo "Cleaning up container services..."
    [ ! -z "$XVFB_PID" ] && kill $XVFB_PID 2>/dev/null || true
    [ ! -z "$DBUS_PID" ] && kill $DBUS_PID 2>/dev/null || true
    pulseaudio --kill 2>/dev/null || true
    wait $XVFB_PID 2>/dev/null || true
    wait $DBUS_PID 2>/dev/null || true
    rm -rf /tmp/dbus-runtime 2>/dev/null || true
    exit 0
}
trap cleanup SIGTERM SIGINT

# Run the application
echo "Starting application: $@"
exec "$@"
