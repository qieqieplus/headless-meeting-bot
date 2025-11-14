package zoombot

import (
	"time"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/stream"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/zoombot/native"
)

// MeetingStatus represents the meeting status from Zoom SDK (mirrors bindings)
type MeetingStatus int

const (
	StatusIdle         MeetingStatus = MeetingStatus(native.StatusIdle)
	StatusConnecting   MeetingStatus = MeetingStatus(native.StatusConnecting)
	StatusInMeeting    MeetingStatus = MeetingStatus(native.StatusInMeeting)
	StatusReconnecting MeetingStatus = MeetingStatus(native.StatusReconnecting)
	StatusFailed       MeetingStatus = MeetingStatus(native.StatusFailed)
	StatusEnded        MeetingStatus = MeetingStatus(native.StatusEnded)
	StatusUnknown      MeetingStatus = MeetingStatus(native.StatusUnknown)
)

func (s MeetingStatus) String() string {
	switch s {
	case StatusIdle:
		return "idle"
	case StatusConnecting:
		return "connecting"
	case StatusInMeeting:
		return "in_meeting"
	case StatusReconnecting:
		return "reconnecting"
	case StatusFailed:
		return "failed"
	case StatusEnded:
		return "ended"
	case StatusUnknown:
		return "unknown"
	default:
		return "unknown"
	}
}

// MeetingConfig holds the configuration for joining a meeting
type MeetingConfig struct {
	MeetingID   string
	Password    string
	DisplayName string
	JoinToken   string
	EnableAudio bool
	EnableVideo bool
	SDKKey      string
	SDKSecret   string
}

type UserEventType int

const (
	UserEventSnapshot         UserEventType = UserEventType(native.UserEventSnapshot)
	UserEventJoined           UserEventType = UserEventType(native.UserEventJoined)
	UserEventLeft             UserEventType = UserEventType(native.UserEventLeft)
	UserEventAudioMuted       UserEventType = UserEventType(native.UserEventAudioMuted)
	UserEventAudioUnmuted     UserEventType = UserEventType(native.UserEventAudioUnmuted)
	UserEventVideoOn          UserEventType = UserEventType(native.UserEventVideoOn)
	UserEventVideoOff         UserEventType = UserEventType(native.UserEventVideoOff)
	UserEventShareStarted     UserEventType = UserEventType(native.UserEventShareStarted)
	UserEventShareStopped     UserEventType = UserEventType(native.UserEventShareStopped)
	UserEventActiveSpeaking   UserEventType = UserEventType(native.UserEventActiveSpeaking)
	UserEventInactiveSpeaking UserEventType = UserEventType(native.UserEventInactiveSpeaking)
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

// MeetingStatistics holds statistics for a meeting
type MeetingStatistics struct {
	StartTime           time.Time `json:"start_time"`
	AudioFramesReceived uint64    `json:"audio_frames_received"`
	AudioFramesDropped  uint64    `json:"audio_frames_dropped"`
	AudioBytesReceived  uint64    `json:"audio_bytes_received"`
	LastAudioSampleTime time.Time `json:"last_audio_time"`
	VideoFilesReceived  uint64    `json:"video_files_received"`
	VideoFilesDropped   uint64    `json:"video_files_dropped"`
	VideoBytesReceived  uint64    `json:"video_bytes_received"`
	LastVideoFileTime   time.Time `json:"last_video_time"`
}

// StatusInfo captures the high-level meeting status along with detail codes and latest error message.
type StatusInfo struct {
	State  string `json:"state"`
	Detail int    `json:"detail"`
	Error  string `json:"error,omitempty"`
}

// MeetingState combines status information and statistics for a meeting instance.
type MeetingState struct {
	MeetingID  string            `json:"meeting_id"`
	Status     StatusInfo        `json:"status"`
	Statistics MeetingStatistics `json:"statistics"`
	Users      []stream.UserInfo `json:"users,omitempty"`
}
