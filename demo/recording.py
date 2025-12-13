#!/usr/bin/env python3
"""
Record meeting from WebSocket API and fetch manifest.

This script connects to the server's WebSocket streams (video, audio, events),
records everything in real-time by saving segments directly to disk,
and fetches the final manifest from the server when the meeting ends.
"""

import argparse
import asyncio
import json
import logging
import shutil
import signal
import sys
import wave
from datetime import datetime
from pathlib import Path
from typing import Optional, Dict

import aiohttp
import requests
import websockets

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s", datefmt="%H:%M:%S")
logger = logging.getLogger("Recorder")

HEADER_SIZE = 64


class RecordingSession:
    def __init__(
        self,
        meeting_id: str,
        output_dir: Optional[str] = None,
        host: str = "localhost",
        port: int = 8080,
        password: Optional[str] = None,
        display_name: str = "RecordingClient",
    ):
        self.meeting_id = meeting_id
        self.host = host
        self.port = port
        self.password = password
        self.display_name = display_name
        self.base_url = f"http://{host}:{port}"
        self.ws_url = f"ws://{host}:{port}"

        if output_dir:
            self.session_dir = Path(output_dir)
        else:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            self.session_dir = Path("recordings") / f"{meeting_id}_{timestamp}"
        self.session_dir.mkdir(parents=True, exist_ok=True)
        
        self.is_running = True
        self.start_time = None
        self.audio_format = {"sample_rate": 32000, "channels": 1}
        self.wav_files: Dict[Path, wave.Wave_write] = {}

    def join_meeting_if_needed(self) -> bool:
        """Join the meeting via REST API."""
        status_url = f"{self.base_url}/api/meetings/{self.meeting_id}"
        try:
            resp = requests.get(status_url, timeout=5)
            if resp.status_code == 200:
                state = resp.json().get("status", {}).get("state")
                if state in {"connecting", "in_meeting"}:
                    logger.info(f"Meeting {self.meeting_id} already active")
                    return True
        except requests.RequestException:
            pass

        payload = {"meeting_id": self.meeting_id, "display_name": self.display_name, "enable_audio": True, "enable_video": True}
        if self.password:
            payload["password"] = self.password

        try:
            resp = requests.post(f"{self.base_url}/api/meetings", json=payload, timeout=10)
            if resp.status_code == 202:
                logger.info(f"Join request accepted for meeting {self.meeting_id}")
                return True
            logger.error(f"Failed to join meeting ({resp.status_code}): {resp.text}")
        except requests.RequestException as e:
            logger.error(f"Failed to join meeting: {e}")
        return False

    def _resolve_path(self, filename: str) -> Optional[Path]:
        """Resolve filename inside session directory, creating subdirs if needed."""
        safe_path = Path(filename)
        while safe_path.is_absolute():
            safe_path = safe_path.relative_to("/")
        resolved = (self.session_dir / safe_path).resolve()
        if not str(resolved).startswith(str(self.session_dir.resolve())):
            logger.warning(f"Security warning: Attempted path traversal {filename}")
            return None
        resolved.parent.mkdir(parents=True, exist_ok=True)
        return resolved

    async def handle_binary_stream(self, stream_type: str):
        """Handle binary streams (audio/video) with 64-byte filename header."""
        url = f"{self.ws_url}/ws/{stream_type}/{self.meeting_id}"
        logger.info(f"Connecting to {stream_type} stream...")
        
        try:
            async with websockets.connect(url, ping_interval=20, ping_timeout=60, max_size=16 * 1024 * 1024) as ws:
                logger.info(f"✅ Connected to {stream_type} stream")
                
                async for message in ws:
                    if not self.is_running:
                        break

                    if isinstance(message, bytes):
                        if len(message) < HEADER_SIZE:
                            continue
                        header = message[:HEADER_SIZE]
                        try:
                            filename = header.split(b"\x00", 1)[0].decode("utf-8")
                            if filename:
                                target = self._resolve_path(filename)
                                if target:
                                    pcm_data = message[HEADER_SIZE:]
                                    
                                    # Handle WAV files with wave module
                                    if stream_type == "audio" and target.suffix.lower() == '.wav':
                                        if target not in self.wav_files:
                                            wav_file = wave.open(str(target), 'wb')
                                            wav_file.setnchannels(self.audio_format["channels"])
                                            wav_file.setsampwidth(2)
                                            wav_file.setframerate(self.audio_format["sample_rate"])
                                            self.wav_files[target] = wav_file
                                        self.wav_files[target].writeframes(pcm_data)
                                    else:
                                        with open(target, 'ab') as f:
                                            f.write(pcm_data)
                        except Exception as e:
                            logger.error(f"Error processing {stream_type} frame: {e}")
                            
                    elif isinstance(message, str):
                        try:
                            payload = json.loads(message)
                            filename = payload.get("filename")
                            content = payload.get("content")
                            if filename and content:
                                target = self._resolve_path(filename)
                                if target:
                                    with open(target, 'w', encoding='utf-8') as f:
                                        f.write(content)
                            elif stream_type == "audio" and payload.get("type") == "audio_format":
                                self.audio_format = {
                                    "sample_rate": payload.get("sample_rate", 32000),
                                    "channels": payload.get("channels", 1),
                                }
                                logger.info(f"🎵 Audio format: {self.audio_format}")
                        except json.JSONDecodeError:
                            pass
                            
        except Exception as e:
            if self.is_running:
                logger.error(f"{stream_type} stream error: {e}")
        finally:
            if stream_type == "audio":
                for wav_file in self.wav_files.values():
                    try:
                        wav_file.close()
                    except Exception:
                        pass
                self.wav_files.clear()

    async def handle_events_stream(self):
        """Handle events stream and watch for meeting end."""
        url = f"{self.ws_url}/ws/events/{self.meeting_id}"
        logger.info("Connecting to events stream...")
        
        try:
            events_file = self.session_dir / "events.ndjson"
            with open(events_file, "a", encoding="utf-8") as f:
                async with websockets.connect(url, ping_interval=20, ping_timeout=60) as ws:
                    logger.info("✅ Connected to events stream")
                    async for message in ws:
                        if not self.is_running:
                            break
                        try:
                            f.write(message + "\n")
                            f.flush()
                            event = json.loads(message)
                            if event.get("type") == "meeting_status" and event.get("status") == "ended":
                                logger.info("🏁 Meeting ended signal received")
                                self.is_running = False
                                return
                        except Exception as e:
                            logger.warning(f"Event processing error: {e}")
        except Exception as e:
            if self.is_running:
                logger.error(f"Events stream error: {e}")

    async def fetch_manifest(self):
        """Fetch the final manifest from the server."""
        logger.info("📝 Fetching manifest from server...")
        url = f"{self.base_url}/api/meetings/{self.meeting_id}/manifest"
        
        for i in range(5):
            try:
                async with aiohttp.ClientSession() as session:
                    async with session.get(url) as resp:
                        if resp.status == 200:
                            manifest = await resp.json()
                            manifest_path = self.session_dir / "manifest.json"
                            with open(manifest_path, "w", encoding="utf-8") as f:
                                json.dump(manifest, f, indent=2, ensure_ascii=False)
                            logger.info(f"✅ Manifest saved to {manifest_path}")
                            return
                        elif resp.status == 404:
                            logger.info("⏳ Manifest not ready yet, retrying...")
            except Exception as e:
                logger.warning(f"Error fetching manifest: {e}")
            await asyncio.sleep(1)
        logger.error("❌ Could not fetch manifest after retries")

    async def record(self):
        """Main recording loop."""
        if not self.join_meeting_if_needed():
            logger.warning("Meeting join failed, but attempting to connect to streams...")

        self.start_time = datetime.now()
        logger.info(f"🎬 Starting recording for meeting {self.meeting_id}")
        logger.info(f"📁 Session directory: {self.session_dir.resolve()}")
        logger.info("Press Ctrl+C to stop...\n")

        tasks = [
            asyncio.create_task(self.handle_binary_stream("video")),
            asyncio.create_task(self.handle_binary_stream("audio")),
            asyncio.create_task(self.handle_events_stream()),
        ]

        try:
            while self.is_running:
                if all(t.done() for t in tasks):
                    break
                await asyncio.sleep(0.1)
        except asyncio.CancelledError:
            pass
        finally:
            self.is_running = False
            for t in tasks:
                t.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
            
            # Close remaining WAV files
            for wav_file in self.wav_files.values():
                try:
                    wav_file.close()
                except Exception:
                    pass
            self.wav_files.clear()
            
            await self.fetch_manifest()
            duration = datetime.now() - self.start_time
            logger.info(f"✅ Recording finished")
            logger.info(f"⏱️  Duration: {duration}")


def clean_recordings(recordings_dir: str = "recordings", pattern: Optional[str] = None, force: bool = False):
    """Clean up recording sessions."""
    base_path = Path(recordings_dir)
    if not base_path.exists():
        logger.error(f"No recordings directory found at {recordings_dir}")
        return

    sessions_to_delete = [item for item in base_path.iterdir() if item.is_dir() and (not pattern or pattern in item.name)]

    if not sessions_to_delete:
        logger.info("No matching sessions found")
        return

    logger.info(f"Found {len(sessions_to_delete)} sessions to delete:")
    for s in sessions_to_delete:
        logger.info(f"  • {s.name}")

    if not force and input("\nDelete these sessions? [y/N] ").lower() != "y":
        logger.info("Cancelled")
        return

    for s in sessions_to_delete:
        try:
            shutil.rmtree(s)
            logger.info(f"🗑️  Deleted {s.name}")
        except Exception as e:
            logger.error(f"Failed to delete {s.name}: {e}")


def main():
    if len(sys.argv) > 1 and sys.argv[1] not in ["record", "clean", "help"] and sys.argv[1] not in ["-h", "--help"]:
        sys.argv.insert(1, "record")

    parser = argparse.ArgumentParser(description="Record meeting from WebSocket API")
    subparsers = parser.add_subparsers(dest="command", help="Command")

    record_parser = subparsers.add_parser("record", help="Record a meeting")
    record_parser.add_argument("meeting_id", help="Meeting ID")
    record_parser.add_argument("--output-dir", help="Output directory")
    record_parser.add_argument("--host", default="localhost", help="Server host")
    record_parser.add_argument("--port", type=int, default=8080, help="Server port")
    record_parser.add_argument("--password", help="Meeting password")
    record_parser.add_argument("--display-name", default="RecordingBot", help="Bot name")

    clean_parser = subparsers.add_parser("clean", help="Clean recordings")
    clean_parser.add_argument("--recordings-dir", default="recordings", help="Recordings dir")
    clean_parser.add_argument("--pattern", help="Filter pattern")
    clean_parser.add_argument("-f", "--force", action="store_true", help="No confirmation")

    subparsers.add_parser("help", help="Show this help message")

    args = parser.parse_args()

    if args.command == "record":
        session = RecordingSession(
            meeting_id=args.meeting_id,
            output_dir=args.output_dir,
            host=args.host,
            port=args.port,
            password=args.password,
            display_name=args.display_name,
        )
        
        def signal_handler(sig, frame):
            logger.info("\n🛑 Stopping recording...")
            session.is_running = False
        
        signal.signal(signal.SIGINT, signal_handler)
        
        try:
            asyncio.run(session.record())
        except KeyboardInterrupt:
            pass
    elif args.command == "clean":
        clean_recordings(args.recordings_dir, args.pattern, args.force)
    elif args.command == "help":
        parser.print_help()
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
