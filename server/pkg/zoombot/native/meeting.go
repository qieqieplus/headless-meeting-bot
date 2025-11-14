//go:build cgo

package native

/*
#include <stdlib.h>
#include "zoom_bot_c.h"

// Forward declarations for callbacks defined in callbacks.go
extern void OnAudioDataReceived(MeetingHandle meeting_handle, void* data, int length, int type, unsigned int node_id);
extern void OnMeetingStatusChanged(MeetingHandle meeting_handle, int status, int detail_code);
extern void OnUserStatusEvent(MeetingHandle meeting_handle, ZoomUserStatusEvent* event);
extern void OnHlsFile(MeetingHandle meeting_handle, char* filename, unsigned char* data, size_t size, int is_playlist, uint64_t sequence);

// C wrapper functions to get callback pointers
static void cAudioCallback(MeetingHandle meeting_handle, const void* data, int length, int type, unsigned int node_id) {
    OnAudioDataReceived(meeting_handle, (void*)data, length, type, node_id);
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
	"unsafe"
)

// Result represents the result of SDK operations
type Result int

const (
	ResultSuccess Result = 0
	ResultError   Result = -1
)

func (r Result) Error() string {
	switch r {
	case ResultSuccess:
		return "success"
	case ResultError:
		return "error"
	default:
		return fmt.Sprintf("unknown result code: %d", int(r))
	}
}

// MeetingStatus represents the meeting status from Zoom SDK
type MeetingStatus int

// Status constants - subset we use (matches ZoomMeetingStatus)
const (
	StatusIdle         MeetingStatus = 0
	StatusConnecting   MeetingStatus = 1
	StatusInMeeting    MeetingStatus = 3
	StatusReconnecting MeetingStatus = 5
	StatusFailed       MeetingStatus = 6
	StatusEnded        MeetingStatus = 7
	StatusUnknown      MeetingStatus = 8
)

// UserEventType represents user event types from C API
type UserEventType int

const (
	UserEventSnapshot         UserEventType = 0
	UserEventJoined           UserEventType = 1
	UserEventLeft             UserEventType = 2
	UserEventAudioMuted       UserEventType = 3
	UserEventAudioUnmuted     UserEventType = 4
	UserEventVideoOn          UserEventType = 5
	UserEventVideoOff         UserEventType = 6
	UserEventShareStarted     UserEventType = 7
	UserEventShareStopped     UserEventType = 8
	UserEventActiveSpeaking   UserEventType = 9
	UserEventInactiveSpeaking UserEventType = 10
)

// UserStatusEvent represents a user status event from C API
type UserStatusEvent struct {
	EventType UserEventType
	UserID    uint64
	Name      string
	Audio     int
	Video     int
	Share     int
	Timestamp uint64
}

// SDKHandle wraps the C SDK handle
type SDKHandle struct {
	handle C.ZoomBotHandle
}

// MeetingHandle wraps the C meeting handle
type MeetingHandle struct {
	handle C.MeetingHandle
}

// HlsVideoConfig holds HLS encoder configuration
type HlsVideoConfig struct {
	Width            int
	Height           int
	FPS              int
	BitrateKbps      int
	Encoder          string
	Preset           string
	HlsPrefix        string
	AudioSampleRate  int
	AudioChannels    int
	AudioBitrateKbps int
}

// RegisterEventSink registers an event sink for this meeting
func (m *MeetingHandle) RegisterEventSink(sink EventSink) {
	if m == nil || m.handle == nil {
		return
	}
	registerMeetingHandle(m.handle, sink)
}

// CreateSDK creates and initializes a new Zoom SDK instance
func CreateSDK(sdkKey, sdkSecret string) (*SDKHandle, error) {
	cKey := C.CString(sdkKey)
	defer C.free(unsafe.Pointer(cKey))

	cSecret := C.CString(sdkSecret)
	defer C.free(unsafe.Pointer(cSecret))

	handle := C.zoom_bot_create(cKey, cSecret)
	if handle == nil {
		return nil, fmt.Errorf("failed to create SDK instance")
	}

	return &SDKHandle{handle: handle}, nil
}

// Destroy cleans up and destroys the SDK instance
func (s *SDKHandle) Destroy() {
	if s.handle != nil {
		C.zoom_bot_destroy(s.handle)
		s.handle = nil
	}
}

// CreateAndJoinMeeting creates and joins a meeting
func (s *SDKHandle) CreateAndJoinMeeting(meetingID, password, displayName, joinToken string, enableAudio, enableVideo bool) (*MeetingHandle, error) {
	if s.handle == nil {
		return nil, fmt.Errorf("SDK handle is nil")
	}

	cMeetingID := C.CString(meetingID)
	defer C.free(unsafe.Pointer(cMeetingID))

	var cPassword *C.char
	if password != "" {
		cPassword = C.CString(password)
		defer C.free(unsafe.Pointer(cPassword))
	}

	var cDisplayName *C.char
	if displayName != "" {
		cDisplayName = C.CString(displayName)
		defer C.free(unsafe.Pointer(cDisplayName))
	}

	var cJoinToken *C.char
	if joinToken != "" {
		cJoinToken = C.CString(joinToken)
		defer C.free(unsafe.Pointer(cJoinToken))
	}

	var cEnableAudio, cEnableVideo C.int
	if enableAudio {
		cEnableAudio = 1
	}
	if enableVideo {
		cEnableVideo = 1
	}

	handle := C.zoom_bot_meeting_create_and_join(s.handle, cMeetingID, cPassword, cDisplayName, cJoinToken, cEnableAudio, cEnableVideo)
	if handle == nil {
		return nil, fmt.Errorf("failed to create and join meeting")
	}

	return &MeetingHandle{handle: handle}, nil
}

// Destroy leaves and destroys the meeting
func (m *MeetingHandle) Destroy() {
	if m.handle != nil {
		unregisterMeetingHandle(m.handle)
		C.zoom_bot_meeting_destroy(m.handle)
		m.handle = nil
	}
}

// SetAudioCallback sets the audio callback for the meeting
func (m *MeetingHandle) SetAudioCallback() error {
	if m.handle == nil {
		return fmt.Errorf("meeting handle is nil")
	}
	result := C.zoom_bot_meeting_set_audio_callback(m.handle, C.getCCallbackPtr())
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

// RunLoop runs the main SDK event loop (blocking)
func RunLoop() {
	C.zoom_bot_run_loop()
}

// StopLoop requests the SDK event loop to stop
func StopLoop() {
	C.zoom_bot_stop_loop()
}
