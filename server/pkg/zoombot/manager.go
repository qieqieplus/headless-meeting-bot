package zoombot

import (
	"fmt"
	"sync"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/stream"
)

// Manager manages multiple meeting instances
type Manager struct {
	meetings  sync.Map // map[string]*MeetingInstance
	audioBus  *stream.AudioBus
	eventsBus *stream.EventBus
	videoBus  *stream.VideoBus
	sdkKey    string
	sdkSecret string
}

// NewManager creates a new meeting manager
func NewManager(sdkKey, sdkSecret string, audioBus *stream.AudioBus, eventsBus *stream.EventBus, videoBus *stream.VideoBus) *Manager {
	return &Manager{
		audioBus:  audioBus,
		eventsBus: eventsBus,
		videoBus:  videoBus,
		sdkKey:    sdkKey,
		sdkSecret: sdkSecret,
	}
}

// JoinMeeting creates and joins a new meeting
func (m *Manager) JoinMeeting(meetingID, password, displayName, joinToken string, enableAudio, enableVideo bool) error {
	if _, exists := m.meetings.Load(meetingID); exists {
		return ErrMeetingAlreadyExists
	}

	config := &MeetingConfig{
		MeetingID:   meetingID,
		Password:    password,
		DisplayName: displayName,
		JoinToken:   joinToken,
		EnableAudio: enableAudio,
		EnableVideo: enableVideo,
		SDKKey:      m.sdkKey,
		SDKSecret:   m.sdkSecret,
	}

	instance := NewMeetingInstance(config, m.audioBus, m.eventsBus, m.videoBus)

	// Start meeting (slow operation)
	if err := instance.Start(); err != nil {
		return fmt.Errorf("failed to start meeting %s: %w", meetingID, err)
	}

	// Add to meetings map (check again for race)
	if _, loaded := m.meetings.LoadOrStore(meetingID, instance); loaded {
		// Stop the instance we created, as another one was created in the meantime
		go instance.Stop()
		return fmt.Errorf("meeting %s already exists", meetingID)
	}

	log.Infof("Successfully joined meeting: %s", meetingID)
	return nil
}

// LeaveMeeting leaves and removes a meeting
func (m *Manager) LeaveMeeting(meetingID string) error {
	value, exists := m.meetings.LoadAndDelete(meetingID)
	if !exists {
		return ErrMeetingNotFound
	}

	instance := value.(*MeetingInstance)
	if err := instance.Stop(); err != nil {
		log.Errorf("Error stopping meeting %s: %v", meetingID, err)
	}

	log.Infof("Successfully left meeting: %s", meetingID)
	return nil
}

// GetMeeting returns a meeting instance by ID
func (m *Manager) GetMeeting(meetingID string) (*MeetingInstance, bool) {
	value, exists := m.meetings.Load(meetingID)
	if !exists {
		return nil, false
	}
	return value.(*MeetingInstance), true
}

// ListMeetings returns all meeting IDs and their status
func (m *Manager) ListMeetings() map[string]StatusInfo {
	result := make(map[string]StatusInfo)
	m.meetings.Range(func(key, value interface{}) bool {
		id := key.(string)
		instance := value.(*MeetingInstance)
		result[id] = instance.GetStatusInfo()
		return true
	})
	return result
}

// Status returns the status for a specific meeting.
func (m *Manager) Status(meetingID string) (StatusInfo, error) {
	value, exists := m.meetings.Load(meetingID)
	if !exists {
		return StatusInfo{}, fmt.Errorf("meeting %s not found", meetingID)
	}

	instance := value.(*MeetingInstance)
	return instance.GetStatusInfo(), nil
}

// Statistics returns the statistics for a specific meeting.
func (m *Manager) Statistics(meetingID string) (MeetingStatistics, error) {
	value, exists := m.meetings.Load(meetingID)
	if !exists {
		return MeetingStatistics{}, fmt.Errorf("meeting %s not found", meetingID)
	}

	instance := value.(*MeetingInstance)
	return instance.GetStatistics(), nil
}

// Users returns the list of users for a specific meeting.
func (m *Manager) Users(meetingID string) ([]stream.UserInfo, error) {
	value, exists := m.meetings.Load(meetingID)
	if !exists {
		return nil, fmt.Errorf("meeting %s not found", meetingID)
	}

	instance := value.(*MeetingInstance)
	return instance.GetUsers(), nil
}

// State returns a bundle of meeting data selected by mask.
func (m *Manager) State(meetingID string, mask StateMask) (MeetingState, error) {
	value, exists := m.meetings.Load(meetingID)
	if !exists {
		return MeetingState{}, ErrMeetingNotFound
	}

	instance := value.(*MeetingInstance)
	state := MeetingState{
		MeetingID: meetingID,
	}

	if mask&StateStatus != 0 {
		state.Status = instance.GetStatusInfo()
	}

	if mask&StateStatistics != 0 {
		state.Statistics = instance.GetStatistics()
	}

	if mask&StateUsers != 0 {
		state.Users = instance.GetUsers()
	}

	return state, nil
}

// Shutdown gracefully shuts down all meetings
func (m *Manager) Shutdown() error {
	log.Info("Shutting down meeting manager")

	var errors []error

	m.meetings.Range(func(key, value interface{}) bool {
		id := key.(string)
		instance := value.(*MeetingInstance)
		log.Infof("Stopping meeting: %s", id)
		if err := instance.Stop(); err != nil {
			log.Errorf("Error stopping meeting %s: %v", id, err)
			errors = append(errors, fmt.Errorf("meeting %s: %w", id, err))
		}
		m.meetings.Delete(id)
		return true
	})

	if len(errors) > 0 {
		return fmt.Errorf("errors during shutdown: %v", errors)
	}

	log.Info("Meeting manager shutdown complete")
	return nil
}

// GetMeetingCount returns the number of active meetings
func (m *Manager) GetMeetingCount() int {
	count := 0
	m.meetings.Range(func(_, _ interface{}) bool {
		count++
		return true
	})
	return count
}
