#!/usr/bin/env python3
"""
Audio Test Recorder for Headless Meeting Bot

This script connects to the WebSocket audio stream and saves meeting audio to WAV files.
"""

import asyncio
import json
import struct
import wave
import argparse
import requests
import websockets
import time
from datetime import datetime
from pathlib import Path


class AudioRecorder:
    def __init__(self, meeting_id: str, output_dir: str = "recordings", host: str = "localhost", port: int = 8080, password: str = None, display_name: str = "AudioRecorder"):
        self.meeting_id = meeting_id
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(exist_ok=True)
        self.host = host
        self.port = port
        self.password = password
        self.display_name = display_name

        # Default audio format (will be updated from frame headers)
        self.sample_rate = 32000  # Default sample rate
        self.channels = 1         # Default mono channel
        self.sample_width = 2     # Default 16-bit
        self.sample_format = "s16le"  # Default format

        # Recording state - multiple files per user
        self.user_files = {}  # user_id -> (wav_file, frame_count, start_time)
        self.current_user_id = None

    def create_wav_filename(self, user_id: int) -> str:
        """Create a unique WAV filename with timestamp for specific user"""
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        return self.output_dir / f"meeting_{self.meeting_id}_user_{user_id}_{timestamp}.wav"

    def get_or_create_user_file(self, user_id: int, channels=1, sample_width=2, sample_rate=32000):
        """Get existing WAV file for user or create new one"""
        if user_id in self.user_files:
            wav_file, frame_count, start_time = self.user_files[user_id]
            return wav_file, frame_count, start_time

        # Create new file for this user
        filename = self.create_wav_filename(user_id)
        print(f"📁 Opening WAV file for user {user_id}: {filename}")

        wav_file = wave.open(str(filename), 'wb')
        wav_file.setnchannels(channels)
        wav_file.setsampwidth(sample_width)
        wav_file.setframerate(sample_rate)

        start_time = time.time()
        self.user_files[user_id] = (wav_file, 0, start_time)
        return wav_file, 0, start_time

    def close_user_file(self, user_id: int):
        """Close WAV file for specific user"""
        if user_id in self.user_files:
            wav_file, frame_count, start_time = self.user_files[user_id]
            duration = time.time() - start_time
            wav_file.close()
            print(f"✅ User {user_id} WAV file closed. Total frames: {frame_count}, Duration: {duration:.1f}s")
            del self.user_files[user_id]

    def close_all_files(self):
        """Close all WAV files"""
        user_count = len(self.user_files)
        for user_id in list(self.user_files.keys()):
            self.close_user_file(user_id)
        print(f"✅ All {user_count} WAV files closed")

    def get_recording_stats(self):
        """Get statistics about recorded users and their files"""
        stats = []
        for user_id, (wav_file, frame_count, start_time) in self.user_files.items():
            duration = time.time() - start_time
            stats.append({
                'user_id': user_id,
                'frame_count': frame_count,
                'duration': duration,
                'filename': wav_file.name if hasattr(wav_file, 'name') else 'unknown'
            })
        return stats

    def join_meeting_if_needed(self):
        """Join the meeting via REST API if not already joined"""
        # Check if meeting is already active
        status_url = f"http://{self.host}:{self.port}/api/meetings/{self.meeting_id}"
        try:
            response = requests.get(status_url, timeout=5)
            if response.status_code == 200:
                status = response.json()
                if status.get("status") in ["joined", "joining"]:
                    print(f"✅ Meeting {self.meeting_id} is already active")
                    return True
        except:
            pass  # Meeting not found or server not running, we'll try to join

        # Join the meeting
        join_url = f"http://{self.host}:{self.port}/api/meetings"
        payload = {
            "meeting_id": self.meeting_id,
            "display_name": self.display_name
        }

        if self.password:
            payload["password"] = self.password
            print(f"🔑 Joining meeting {self.meeting_id} with password")
        else:
            print(f"🔗 Joining meeting {self.meeting_id} without password")

        try:
            response = requests.post(join_url, json=payload, timeout=10)
            if response.status_code == 202:  # HTTP 202 Accepted
                print(f"✅ Meeting join request sent for {self.meeting_id}")
                return True
            else:
                print(f"❌ Failed to join meeting: {response.status_code} - {response.text}")
                return False
        except requests.exceptions.RequestException as e:
            print(f"❌ Failed to join meeting: {e}")
            return False

    async def handle_text_message(self, message: str):
        """Handle incoming text (JSON) message"""
        try:
            data = json.loads(message)
            msg_type = data.get("type")
            
            if msg_type == "audio_format":
                # Initial format message
                self.sample_rate = data.get("sample_rate", 32000)
                self.channels = data.get("channels", 1)
                self.sample_format = data.get("sample_format", "s16le")
                self.sample_width = 2  # s16le is always 2 bytes
                
                print(f"🎵 Audio format: {self.sample_rate}Hz, {self.channels}ch, {self.sample_format}")
            elif msg_type == "error":
                print(f"❌ Server error: {data.get('error')}")
            elif msg_type == "heartbeat":
                pass  # Ignore heartbeats
            else:
                print(f"ℹ️  Unknown message type: {msg_type}")
        except json.JSONDecodeError as e:
            print(f"❌ Failed to parse JSON message: {e}")

    async def handle_binary_message(self, message: bytes):
        """Handle incoming binary audio frame"""
        # New binary frame format: [8:type][8:user_id][payload...]
        if len(message) < 16:
            print(f"❌ Message too short for frame header: {len(message)} bytes")
            return

        try:
            # Parse frame header
            audio_type = struct.unpack('<Q', message[0:8])[0]
            user_id = struct.unpack('<Q', message[8:16])[0]

            # Get or create WAV file for this user
            wav_file, frame_count, start_time = self.get_or_create_user_file(
                user_id, self.channels, self.sample_width, self.sample_rate
            )

            # Extract PCM data (remaining bytes after 16-byte header)
            audio_data = message[16:]

            # Write to user's WAV file
            wav_file.writeframes(audio_data)

            # Update frame count for this user
            self.user_files[user_id] = (wav_file, frame_count + 1, start_time)

            if (frame_count + 1) % 100 == 0:  # Print progress every 100 frames for this user
                duration = time.time() - start_time
                audio_type_str = ["mixed", "one_way", "share"][audio_type] if audio_type < 3 else "unknown"
                print(f"🎵 User {user_id} ({audio_type_str}): Recorded {frame_count + 1} frames ({duration:.1f}s)")

        except struct.error as e:
            print(f"❌ Failed to parse binary frame: {e}")

    async def record_audio(self, websocket_url: str):
        """Main recording function"""
        # First, try to join the meeting if needed
        if not self.join_meeting_if_needed():
            print("❌ Cannot proceed with recording - failed to join meeting")
            # return

        print(f"🔗 Connecting to WebSocket: {websocket_url}")

        try:
            async with websockets.connect(websocket_url) as websocket:
                print("✅ Connected to WebSocket")
                print("🎵 Waiting for audio format message...")

                while True:
                    try:
                        message = await websocket.recv()
                        if isinstance(message, bytes):
                            await self.handle_binary_message(message)
                        elif isinstance(message, str):
                            await self.handle_text_message(message)
                        else:
                            print(f"❌ Unexpected message type: {type(message)}")

                    except websockets.exceptions.ConnectionClosed:
                        print("🔌 WebSocket connection closed")
                        break

        except Exception as e:
            print(f"❌ WebSocket error: {e}")
        finally:
            # Show recording summary
            stats = self.get_recording_stats()
            if stats:
                print("\n📊 Recording Summary:")
                for stat in stats:
                    print(f"  User {stat['user_id']}: {stat['frame_count']} frames, {stat['duration']:.1f}s")
            else:
                print("\n📊 No audio recorded")

            self.close_all_files()
            print("🏁 Recording session ended")


def main():
    parser = argparse.ArgumentParser(description="Record meeting audio to separate WAV files per user")
    parser.add_argument("meeting_id", help="Meeting ID to record")
    parser.add_argument("--host", default="127.0.0.1", help="Server host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8080, help="Server port (default: 8080)")
    parser.add_argument("--output-dir", default="recordings", help="Output directory for WAV files")
    parser.add_argument("--password", help="Meeting password (optional)")
    parser.add_argument("--display-name", default="AudioRecorder", help="Display name for joining meeting")

    args = parser.parse_args()

    # Construct WebSocket URL
    websocket_url = f"ws://{args.host}:{args.port}/ws/audio/{args.meeting_id}"

    print("🎙️  Headless Meeting Bot - Multi-User Audio Recorder")
    print(f"📍 Meeting ID: {args.meeting_id}")
    if args.password:
        print(f"🔑 Password: {'*' * len(args.password)}")
    else:
        print("🔓 No password provided")
    print(f"👤 Display Name: {args.display_name}")
    print(f"🌐 WebSocket: {websocket_url}")
    print(f"💾 Output dir: {args.output_dir} (separate files per user)")
    print("---")
    print("Press Ctrl+C to stop recording...")

    # Create recorder instance
    recorder = AudioRecorder(
        args.meeting_id,
        args.output_dir,
        args.host,
        args.port,
        args.password,
        args.display_name
    )

    try:
        # Run the recording
        asyncio.run(recorder.record_audio(websocket_url))
    except KeyboardInterrupt:
        print("\n🛑 Recording stopped by user")
    except Exception as e:
        print(f"\n❌ Recording failed: {e}")


if __name__ == "__main__":
    main()
