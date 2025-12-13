//go:build cgo

package native

/*
#include "zoom_bot_c.h"
*/
import "C"

import "fmt"

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

// Status constants - matches ZoomMeetingStatus enum from C API
const (
	StatusIdle              MeetingStatus = 0
	StatusConnecting        MeetingStatus = 1
	StatusWaitingForHost    MeetingStatus = 2
	StatusInMeeting         MeetingStatus = 3
	StatusDisconnecting     MeetingStatus = 4
	StatusReconnecting      MeetingStatus = 5
	StatusFailed            MeetingStatus = 6
	StatusEnded             MeetingStatus = 7
	StatusUnknown           MeetingStatus = 8
	StatusLocked            MeetingStatus = 9
	StatusUnlocked          MeetingStatus = 10
	StatusInWaitingRoom     MeetingStatus = 11
	StatusWebinarPromote    MeetingStatus = 12
	StatusWebinarDepromote  MeetingStatus = 13
	StatusJoinBreakoutRoom  MeetingStatus = 14
	StatusLeaveBreakoutRoom MeetingStatus = 15
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

// AudioType constants match ZOOM_AUDIO_TYPE_* from C API
const (
	AudioTypeMixed  = 0 // Mixed audio from all participants
	AudioTypeOneWay = 1 // One-way audio from a specific user
	AudioTypeShare  = 2 // Audio from screen share
)

// UserStatusEvent represents a user status event from C API
type UserStatusEvent struct {
	EventType UserEventType
	UserID    uint64
	Name      string
	Audio     int
	Video     int
	Share     int
	WallTs    uint64 // Absolute unix epoch timestamp in milliseconds
	MediaTs   int64  // Media timeline timestamp in ms (can be negative for pre-recording events)
}

// SDKHandle wraps the C SDK handle
type SDKHandle struct {
	handle C.ZoomBotHandle
}

// MeetingHandle wraps the C meeting handle
type MeetingHandle struct {
	handle C.MeetingHandle
}

// AudioConfig holds raw audio callback configuration
type AudioConfig struct {
	SampleRate  int    // Sample rate in Hz (e.g., 32000, 16000)
	Channels    int    // Number of channels (1 for mono, 2 for stereo)
	Encoding    string // Encoding format: "S16LE", "AAC", "MP3"
	BitrateKbps int    // Bitrate in kbps (default: 128)
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
