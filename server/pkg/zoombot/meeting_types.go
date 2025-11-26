package zoombot

import (
	"time"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/stream"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/zoombot/native"
)

// MeetingStatus represents the meeting status from Zoom SDK (mirrors bindings)
type MeetingStatus int

const (
	StatusIdle              MeetingStatus = MeetingStatus(native.StatusIdle)
	StatusConnecting        MeetingStatus = MeetingStatus(native.StatusConnecting)
	StatusWaitingForHost    MeetingStatus = MeetingStatus(native.StatusWaitingForHost)
	StatusInMeeting         MeetingStatus = MeetingStatus(native.StatusInMeeting)
	StatusDisconnecting     MeetingStatus = MeetingStatus(native.StatusDisconnecting)
	StatusReconnecting      MeetingStatus = MeetingStatus(native.StatusReconnecting)
	StatusFailed            MeetingStatus = MeetingStatus(native.StatusFailed)
	StatusEnded             MeetingStatus = MeetingStatus(native.StatusEnded)
	StatusUnknown           MeetingStatus = MeetingStatus(native.StatusUnknown)
	StatusLocked            MeetingStatus = MeetingStatus(native.StatusLocked)
	StatusUnlocked          MeetingStatus = MeetingStatus(native.StatusUnlocked)
	StatusInWaitingRoom     MeetingStatus = MeetingStatus(native.StatusInWaitingRoom)
	StatusWebinarPromote    MeetingStatus = MeetingStatus(native.StatusWebinarPromote)
	StatusWebinarDepromote  MeetingStatus = MeetingStatus(native.StatusWebinarDepromote)
	StatusJoinBreakoutRoom  MeetingStatus = MeetingStatus(native.StatusJoinBreakoutRoom)
	StatusLeaveBreakoutRoom MeetingStatus = MeetingStatus(native.StatusLeaveBreakoutRoom)
)

func (s MeetingStatus) String() string {
	switch s {
	case StatusIdle:
		return "idle"
	case StatusConnecting:
		return "connecting"
	case StatusWaitingForHost:
		return "waiting_for_host"
	case StatusInMeeting:
		return "in_meeting"
	case StatusDisconnecting:
		return "disconnecting"
	case StatusReconnecting:
		return "reconnecting"
	case StatusFailed:
		return "failed"
	case StatusEnded:
		return "ended"
	case StatusUnknown:
		return "unknown"
	case StatusLocked:
		return "locked"
	case StatusUnlocked:
		return "unlocked"
	case StatusInWaitingRoom:
		return "in_waiting_room"
	case StatusWebinarPromote:
		return "webinar_promote"
	case StatusWebinarDepromote:
		return "webinar_depromote"
	case StatusJoinBreakoutRoom:
		return "join_breakout_room"
	case StatusLeaveBreakoutRoom:
		return "leave_breakout_room"
	default:
		return "unknown"
	}
}

// MeetingConfig holds the configuration for joining a meeting
type MeetingConfig struct {
	MeetingID     string
	Password      string
	DisplayName   string
	JoinToken     string
	EnableAudio   bool
	EnableVideo   bool
	SDKKey        string
	SDKSecret     string
	AudioEncoding string
	AudioBitrate  int
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

// AudioType constants for audio stream types
const (
	AudioTypeMixed  = native.AudioTypeMixed  // Mixed audio from all participants
	AudioTypeOneWay = native.AudioTypeOneWay // One-way audio from a specific user
	AudioTypeShare  = native.AudioTypeShare  // Audio from screen share
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
