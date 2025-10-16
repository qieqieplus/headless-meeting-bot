package zoomsdk

// MeetingManager is the interface for managing meetings
// Both Manager (in-process) and ProcessManager (multi-process) implement this interface
type MeetingManager interface {
	JoinMeeting(meetingID, password, displayName, joinToken string, enableAudio, enableVideo bool) error
	LeaveMeeting(meetingID string) error
	ListMeetings() map[string]MeetingStatus
	GetMeetingStats(meetingID string) (*MeetingStats, error)
	GetAllStats() map[string]MeetingStats
	Shutdown() error
	GetMeetingCount() int
}

// MeetingInfo provides common information about a meeting
type MeetingInfo interface {
	GetStatus() MeetingStatus
	GetError() error
}
