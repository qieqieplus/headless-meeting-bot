# Headless Meeting Bot Demo

A simple demo for recording Zoom meetings and playing back HLS streams.

## Quick Start

1. **Start the demo server:**
   ```bash
   cd demo
   ./serve.sh
   ```

2. **Set Zoom SDK credentials:**
   ```bash
   # Option 1: Set environment variables directly
   export ZOOM_SDK_KEY="your_sdk_key"
   export ZOOM_SDK_SECRET="your_sdk_secret"

   # Option 2: Use .env file (recommended)
   cp .env.example .env
   # Edit .env with your credentials
   ```

3. **Record a meeting:**
   ```bash
   ./record.sh <meeting_id> <password>
   ```

4. **Play the recording:**
   - Open http://localhost:8000 in your browser
   - The HLS player will automatically load `./video/media.m3u8`
   - Or manually enter any HLS path to test other streams

## Advanced Usage

### Environment Configuration

The script automatically loads environment variables from a `.env` file if it exists:

```bash
# Copy the example and edit with your credentials
cp .env.example .env
nano .env  # Add your ZOOM_SDK_KEY and ZOOM_SDK_SECRET
```

### Custom Paths

The `record.sh` script supports environment variables for customization:

```bash
# Use custom output directory
OUTPUT_DIR=my_recordings ./record.sh 123456789 password

# Use custom build location
BUILD_DIR=/path/to/build EXECUTABLE=/path/to/bot ./record.sh 123456789 password

# Use custom source directory
SRC_DIR=/path/to/src ./record.sh 123456789 password

# Use custom .env file location
ENV_FILE=/path/to/my/.env ./record.sh 123456789 password
```

### Help

```bash
./record.sh --help
```

## Files

- `index.html` - HLS video player with custom path input
- `record.sh` - Records Zoom meetings to HLS format (fully parameterized)
- `serve.sh` - Starts web server with instructions
- `.env.example` - Example environment configuration file
- `.env` - Your personal environment configuration (create from .env.example)
- `video/` - Directory where recordings are saved (default)
- `media.*` - Existing sample HLS files (can be removed)
