//go:build cgo

package native

/*
#include <stdlib.h>
#include "zoom_bot_c.h"

// Forward declarations for callbacks defined in callback.go
extern void OnAudioDataReceived(MeetingHandle meeting_handle, void* data, int length, int type, unsigned int node_id, const char* filename);
extern void OnMeetingStatusChanged(MeetingHandle meeting_handle, int status, int detail_code);
extern void OnUserStatusEvent(MeetingHandle meeting_handle, ZoomUserStatusEvent* event);
extern void OnHlsFile(MeetingHandle meeting_handle, char* filename, unsigned char* data, size_t size, int is_playlist, uint64_t sequence);

// C wrapper functions to get callback pointers
static void cAudioCallback(MeetingHandle meeting_handle, const void* data, int length, int type, unsigned int node_id, const char* filename) {
    OnAudioDataReceived(meeting_handle, (void*)data, length, type, node_id, filename);
}

static OnAudioDataReceivedCallback getCCallbackPtr() {
    return cAudioCallback;
}

static void cMeetingStatusCallback(MeetingHandle meeting_handle, ZoomMeetingStatus status, int detail_code) {
    OnMeetingStatusChanged(meeting_handle, (int)status, detail_code);
}

static OnMeetingStatusCallback getMeetingStatusCallbackPtr() {
    return cMeetingStatusCallback;
}

static void cUserStatusCallback(MeetingHandle meeting_handle, const ZoomUserStatusEvent* event) {
    OnUserStatusEvent(meeting_handle, (ZoomUserStatusEvent*)event);
}

static OnUserStatusEventCallback getUserStatusCallbackPtr() {
    return cUserStatusCallback;
}

static void cHlsFileCallback(MeetingHandle meeting_handle, const char* filename, const unsigned char* data, size_t size, int is_playlist, uint64_t sequence) {
    OnHlsFile(meeting_handle, (char*)filename, (unsigned char*)data, size, is_playlist, sequence);
}

static OnHlsFileCallback getHlsFileCallbackPtr() {
    return cHlsFileCallback;
}
*/
import "C"

import (
	"fmt"
	"strings"
	"unsafe"
)

// RegisterEventSink registers an event sink for this meeting
func (m *MeetingHandle) RegisterEventSink(sink EventSink) {
	if m == nil || m.handle == nil {
		return
	}
	registerMeetingHandle(m.handle, sink)
}

// Destroy leaves and destroys the meeting
func (m *MeetingHandle) Destroy() {
	if m.handle != nil {
		unregisterMeetingHandle(m.handle)
		C.zoom_bot_meeting_destroy(m.handle)
		m.handle = nil
	}
}

// SetAudioCallback sets the audio callback for the meeting with optional configuration
func (m *MeetingHandle) SetAudioCallback(config *AudioConfig) error {
	if m.handle == nil {
		return fmt.Errorf("meeting handle is nil")
	}
	var cConfig *C.ZoomAudioConfig
	if config != nil {
		var cEncoding C.ZoomAudioEncoding
		// Normalize encoding string to uppercase for case-insensitive matching
		encodingUpper := strings.ToUpper(strings.TrimSpace(config.Encoding))
		switch encodingUpper {
		case "AAC":
			cEncoding = C.ZOOM_AUDIO_ENCODING_AAC
		case "MP3":
			cEncoding = C.ZOOM_AUDIO_ENCODING_MP3
		case "S16LE", "PCM", "":
			cEncoding = C.ZOOM_AUDIO_ENCODING_S16LE
		default:
			// Default to S16LE for unknown encodings
			cEncoding = C.ZOOM_AUDIO_ENCODING_S16LE
		}

		bitrate := config.BitrateKbps
		if bitrate <= 0 {
			bitrate = 128 // Default
		}

		cConfig = &C.ZoomAudioConfig{
			sample_rate:  C.int(config.SampleRate),
			channels:     C.int(config.Channels),
			encoding:     cEncoding,
			bitrate_kbps: C.int(bitrate),
		}
	}
	result := C.zoom_bot_meeting_set_audio_callback(m.handle, C.getCCallbackPtr(), cConfig)
	if result != C.ZOOM_BOT_SUCCESS {
		return fmt.Errorf("failed to set audio callback: %s", Result(result).Error())
	}
	return nil
}

// SetStatusCallback registers the meeting status callback
func (m *MeetingHandle) SetStatusCallback() error {
	if m.handle == nil {
		return fmt.Errorf("meeting handle is nil")
	}
	result := C.zoom_bot_meeting_set_status_callback(m.handle, C.getMeetingStatusCallbackPtr())
	if result != C.ZOOM_BOT_SUCCESS {
		return fmt.Errorf("failed to set meeting status callback: %s", Result(result).Error())
	}
	return nil
}

// SetUserStatusCallback registers the user status callback
func (m *MeetingHandle) SetUserStatusCallback() error {
	if m.handle == nil {
		return fmt.Errorf("meeting handle is nil")
	}
	result := C.zoom_bot_meeting_set_user_status_callback(m.handle, C.getUserStatusCallbackPtr())
	if result != C.ZOOM_BOT_SUCCESS {
		return fmt.Errorf("failed to set user status callback: %s", Result(result).Error())
	}
	return nil
}

// SetHlsVideoCallback registers the HLS file callback with configuration
func (m *MeetingHandle) SetHlsVideoCallback(config *HlsVideoConfig) error {
	if m.handle == nil {
		return fmt.Errorf("meeting handle is nil")
	}
	var cConfig *C.ZoomHlsVideoConfig
	if config != nil {
		cEncoder := C.CString(config.Encoder)
		defer C.free(unsafe.Pointer(cEncoder))
		cPreset := C.CString(config.Preset)
		defer C.free(unsafe.Pointer(cPreset))
		cPrefix := C.CString(config.HlsPrefix)
		defer C.free(unsafe.Pointer(cPrefix))
		cConfig = &C.ZoomHlsVideoConfig{
			width:              C.int(config.Width),
			height:             C.int(config.Height),
			fps:                C.int(config.FPS),
			bitrate_kbps:       C.int(config.BitrateKbps),
			encoder:            cEncoder,
			preset:             cPreset,
			hls_prefix:         cPrefix,
			audio_sample_rate:  C.int(config.AudioSampleRate),
			audio_channels:     C.int(config.AudioChannels),
			audio_bitrate_kbps: C.int(config.AudioBitrateKbps),
		}
	}
	result := C.zoom_bot_meeting_set_hls_video_callback(m.handle, C.getHlsFileCallbackPtr(), cConfig)
	if result != C.ZOOM_BOT_SUCCESS {
		return fmt.Errorf("failed to set HLS video callback: %s", Result(result).Error())
	}
	return nil
}
