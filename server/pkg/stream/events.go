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
	UserEventSnapshot     UserEventType = "snapshot"
	UserEventJoined       UserEventType = "joined"
	UserEventLeft         UserEventType = "left"
	UserEventAudioMuted   UserEventType = "audio_muted"
	UserEventAudioUnmuted UserEventType = "audio_unmuted"
	UserEventVideoOn      UserEventType = "video_on"
	UserEventVideoOff     UserEventType = "video_off"
	UserEventShareStarted UserEventType = "share_started"
	UserEventShareStopped UserEventType = "share_stopped"
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
	Type EventType `json:"type"`
	TS   int64     `json:"ts"`

	// Meeting status fields (when Type == EventTypeMeetingStatus)
	Status string `json:"status,omitempty"`
	Detail int    `json:"detail,omitempty"`

	// User event fields (when Type == EventTypeUserEvent)
	Event UserEventType `json:"event,omitempty"`
	User  UserInfo      `json:"user,omitempty"`

	// Server-side timestamp for internal metrics; not serialized.
	createdAt time.Time `json:"-"`
}

// NewMeetingStatusEvent creates a new meeting status event
func NewMeetingStatusEvent(meetingID, status string, detail int) *Event {
	return &Event{
		MeetingID: meetingID,
		Type:      EventTypeMeetingStatus,
		Status:    status,
		Detail:    detail,
		TS:        time.Now().UnixMilli(),
		createdAt: time.Now(),
	}
}

// NewUserEvent creates a new user event
func NewUserEvent(meetingID string, eventType UserEventType, user UserInfo, timestamp int64) *Event {
	return &Event{
		MeetingID: meetingID,
		Type:      EventTypeUserEvent,
		Event:     eventType,
		User:      user,
		TS:        timestamp,
		createdAt: time.Now(),
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
