package zoombot

import "github.com/qieqieplus/headless-meeting-bot/server/pkg/stream"

// MeetingManager manages meetings and exposes data access
// Both Manager (in-process) and ProcessManager (multi-process) implement this interface
type MeetingManager interface {
	JoinMeeting(meetingID, password, displayName, joinToken string, enableAudio, enableVideo bool) error
	LeaveMeeting(meetingID string) error
	ListMeetings() map[string]StatusInfo
	Shutdown() error
	GetMeetingCount() int
	MeetingDataCollector // Status/Statistics/Users/State
}

// StateMask indicates which fields to include when collecting meeting state in a single call.
// Use bitwise OR to combine multiple fields, e.g., StateStatus|StateUsers.
type StateMask uint32

const (
	// StateStatus includes meeting status information.
	StateStatus StateMask = 1 << iota
	// StateStatistics includes meeting statistics.
	StateStatistics
	// StateUsers includes the list of users in the meeting.
	StateUsers
	// StateAll is a convenience mask that includes all fields.
	StateAll = StateStatus | StateStatistics | StateUsers
)

// MeetingDataCollector provides fine-grained accessors for in-process usage and
// a bundled collection API intended for multi-process transports.
// Implementations should prefer returning a single bundle for remote calls to minimize round-trips.
type MeetingDataCollector interface {
	// Status returns current meeting status.
	Status(meetingID string) (StatusInfo, error)
	// Statistics returns current meeting statistics.
	Statistics(meetingID string) (MeetingStatistics, error)
	// Users returns the current list of users.
	Users(meetingID string) ([]stream.UserInfo, error)

	// State returns a bundle of meeting data selected by mask.
	// Implementations should only compute/fetch selected fields.
	State(meetingID string, mask StateMask) (MeetingState, error)
}
