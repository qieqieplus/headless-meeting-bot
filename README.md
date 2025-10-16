# Headless Meeting Bot for Zoom

A bot to join Zoom meetings programmatically on servers. Records audio and shared screens.

## Quick Start

```bash
export ZOOM_SDK_KEY="your_key" ZOOM_SDK_SECRET="your_secret"
cd src/build && make headless_zoom_bot_c

# Record audio only
./headless_zoom_bot_c audio <meeting_id> <password>

# Record shared screens (auto-includes audio)
./headless_zoom_bot_c video <meeting_id> <password>
```

## Features

- **Audio Recording**: PCM audio from all participants
- **Screen Recording**: Shared screen content in YUV420 format
- **Event-Driven**: Auto-subscribes when someone shares their screen
- **Simple API**: Two modes (audio / video), single executable

## C API

```c
ZoomSDKHandle sdk = zoom_sdk_create(key, secret);
MeetingHandle meeting = zoom_meeting_create_and_join(sdk, id, pwd, "Bot", token, 1, 1);
zoom_meeting_set_video_callback(meeting, callback);  // Receives shared screens
zoom_sdk_run_loop();
```

## Go Server

See [server/README.md](server/README.md) for REST/WebSocket API to control the bot.
