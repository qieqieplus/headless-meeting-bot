package zoomsdk

import (
	"fmt"
	"sync"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/audio"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
)

// Manager manages multiple meeting instances
type Manager struct {
	meetings  sync.Map // map[string]*MeetingInstance
	audioBus  *audio.Bus
	sdkKey    string
	sdkSecret string
}

// NewManager creates a new meeting manager
func NewManager(sdkKey, sdkSecret string, audioBus *audio.Bus) *Manager {
	return &Manager{
		audioBus:  audioBus,
		sdkKey:    sdkKey,
		sdkSecret: sdkSecret,
	}
}

// JoinMeeting creates and joins a new meeting
func (m *Manager) JoinMeeting(meetingID, password, displayName, joinToken string, enableAudio, enableVideo bool) error {
	if _, exists := m.meetings.Load(meetingID); exists {
		return fmt.Errorf("meeting %s already exists", meetingID)
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

	instance := NewMeetingInstance(config, m.audioBus)

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
		return fmt.Errorf("meeting %s not found", meetingID)
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
func (m *Manager) ListMeetings() map[string]MeetingStatus {
	result := make(map[string]MeetingStatus)
	m.meetings.Range(func(key, value interface{}) bool {
		id := key.(string)
		instance := value.(*MeetingInstance)
		result[id] = instance.GetStatus()
		return true
	})
	return result
}

// GetMeetingStats returns statistics for a specific meeting
func (m *Manager) GetMeetingStats(meetingID string) (*MeetingStats, error) {
	value, exists := m.meetings.Load(meetingID)
	if !exists {
		return nil, fmt.Errorf("meeting %s not found", meetingID)
	}

	instance := value.(*MeetingInstance)
	stats := instance.GetStats()
	return &stats, nil
}

// GetAllStats returns statistics for all meetings
func (m *Manager) GetAllStats() map[string]MeetingStats {
	result := make(map[string]MeetingStats)
	m.meetings.Range(func(key, value interface{}) bool {
		id := key.(string)
		instance := value.(*MeetingInstance)
		result[id] = instance.GetStats()
		return true
	})
	return result
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
