package stream

import "time"

// EventType represents the type of event
type EventType string

const (
	EventTypeMeetingStatus EventType = "meeting_status"
	EventTypeUserEvent     EventType = "user_event"
)

// UserEventType represents user event types from C API
type UserEventType string

const (
	UserEventSnapshot         UserEventType = "snapshot"
	UserEventJoined           UserEventType = "joined"
	UserEventLeft             UserEventType = "left"
	UserEventAudioMuted       UserEventType = "audio_muted"
	UserEventAudioUnmuted     UserEventType = "audio_unmuted"
	UserEventVideoOn          UserEventType = "video_on"
	UserEventVideoOff         UserEventType = "video_off"
	UserEventShareStarted     UserEventType = "share_started"
	UserEventShareStopped     UserEventType = "share_stopped"
	UserEventActiveSpeaking   UserEventType = "active_speaking"
	UserEventInactiveSpeaking UserEventType = "inactive_speaking"
)

// UserInfo represents user information
type UserInfo struct {
	ID    uint64 `json:"id"`
	Name  string `json:"name"`
	Audio int    `json:"audio"` // 1 if audio on, 0 otherwise
	Video int    `json:"video"` // 1 if video on, 0 otherwise
	Share int    `json:"share"` // 1 if sharing, 0 otherwise
}

// Event is the unified event object for both meeting status and user events.
// It is used end-to-end: produced by the worker, transported over NDJSON, and sent to WebSocket clients.
type Event struct {
	// Transport-only identifier to scope delivery and routing between processes.
	MeetingID string `json:"meeting_id"`

	// Common fields
	Type    EventType `json:"type"`
	WallTs  int64     `json:"wallTs"`            // Absolute unix epoch timestamp (ms)
	MediaTs int64     `json:"mediaTs,omitempty"` // Media timeline timestamp (ms, may be negative)

	// Meeting status fields (when Type == EventTypeMeetingStatus)
	Status string `json:"status,omitempty"`
	Detail int    `json:"detail,omitempty"`

	// User event fields (when Type == EventTypeUserEvent)
	Event UserEventType `json:"event,omitempty"`
	User  UserInfo      `json:"user,omitempty"`
}

// NewMeetingStatusEvent creates a new meeting status event
func NewMeetingStatusEvent(meetingID, status string, detail int) *Event {
	wallTs := time.Now().UnixMilli()
	return &Event{
		MeetingID: meetingID,
		Type:      EventTypeMeetingStatus,
		Status:    status,
		Detail:    detail,
		WallTs:    wallTs,
	}
}

// NewUserEvent creates a new user event with wall timestamp and optional media timeline timestamp
func NewUserEvent(meetingID string, eventType UserEventType, user UserInfo, wallTs int64, mediaTs int64) *Event {
	return &Event{
		MeetingID: meetingID,
		Type:      EventTypeUserEvent,
		Event:     eventType,
		User:      user,
		WallTs:    wallTs,
		MediaTs:   mediaTs,
	}
}

// EventSubscriber represents a client subscribed to event streams.
type EventSubscriber = Subscriber[*Event]

// NewEventSubscriber creates a new event subscriber.
func NewEventSubscriber(id string, bufferSize int) *EventSubscriber {
	return NewSubscriber[*Event](id, bufferSize)
}

// EventBus manages event distribution to subscribers.
type EventBus = Bus[*Event]

// NewEventBus creates a new event bus.
func NewEventBus() *EventBus {
	return NewBus[*Event]("events")
}
