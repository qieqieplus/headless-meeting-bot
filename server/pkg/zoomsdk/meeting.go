package zoomsdk

import (
	"fmt"
	"time"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/audio"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
)

// MeetingStatus represents the meeting status from Zoom SDK
type MeetingStatus int

// Status constants - only the ones we care about (from ZoomMeetingStatus in C API)
const (
	StatusIdle         MeetingStatus = 0 // MEETING_STATUS_IDLE
	StatusConnecting   MeetingStatus = 1 // MEETING_STATUS_CONNECTING
	StatusInMeeting    MeetingStatus = 3 // MEETING_STATUS_INMEETING
	StatusReconnecting MeetingStatus = 5 // MEETING_STATUS_RECONNECTING
	StatusFailed       MeetingStatus = 6 // MEETING_STATUS_FAILED
	StatusEnded        MeetingStatus = 7 // MEETING_STATUS_ENDED
	StatusUnknown      MeetingStatus = 8 // MEETING_STATUS_UNKNOWN
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

// MeetingInstance represents a single meeting session
type MeetingInstance struct {
	meetingID string
	config    *MeetingConfig

	// SDK handles
	sdkHandle     *SDKHandle
	meetingHandle *MeetingHandle

	// OS thread for SDK operations
	osThread *OSThread

	// Audio processing
	audioChannel chan *audio.AudioFrame
	audioBus     *audio.Bus

	// Lifecycle
	stopChan chan struct{}
	stopped  bool

	// Error tracking
	lastError error

	// Statistics
	stats MeetingStats
}

// MeetingStats holds statistics for a meeting
type MeetingStats struct {
	StartTime      time.Time
	FramesReceived uint64
	FramesDropped  uint64
	BytesReceived  uint64
	LastFrameTime  time.Time
}

// NewMeetingInstance creates a new meeting instance
func NewMeetingInstance(config *MeetingConfig, audioBus *audio.Bus) *MeetingInstance {
	return &MeetingInstance{
		meetingID:    config.MeetingID,
		config:       config,
		audioChannel: make(chan *audio.AudioFrame, 1000), // Buffer for audio frames
		audioBus:     audioBus,
		stopChan:     make(chan struct{}),
		stats: MeetingStats{
			StartTime: time.Now(),
		},
	}
}

// GetStatus returns the current meeting status from Zoom SDK
func (m *MeetingInstance) GetStatus() MeetingStatus {
	if m.meetingHandle == nil {
		return StatusIdle
	}
	return m.meetingHandle.GetStatus()
}

// GetError returns the last error
func (m *MeetingInstance) GetError() error {
	return m.lastError
}

// GetStats returns meeting statistics
func (m *MeetingInstance) GetStats() MeetingStats {
	return m.stats
}

// Start starts the meeting instance and joins the meeting
func (m *MeetingInstance) Start() error {
	if m.GetStatus() != StatusIdle {
		return fmt.Errorf("meeting is not in idle state")
	}

	log.Infof("Starting meeting instance for meeting ID: %s", m.meetingID)

	// Create and start OS thread
	m.osThread = NewOSThread()
	m.osThread.Start()

	// Start audio frame processor
	go m.processAudioFrames()

	// Join meeting on OS thread
	var joinError error
	m.osThread.Execute(func() {
		joinError = m.joinMeeting()
	})

	if joinError != nil {
		m.lastError = joinError
		return joinError
	}

	// Start SDK event loop on OS thread
	go func() {
		m.osThread.Execute(func() {
			RunLoop()
		})
	}()

	return nil
}

// joinMeeting performs the actual meeting join operation
func (m *MeetingInstance) joinMeeting() error {
	// Create SDK instance
	sdk, err := CreateSDK(m.config.SDKKey, m.config.SDKSecret)
	if err != nil {
		return fmt.Errorf("failed to create SDK: %w", err)
	}
	m.sdkHandle = sdk

	// Create and join meeting
	meeting, err := sdk.CreateAndJoinMeeting(
		m.config.MeetingID,
		m.config.Password,
		m.config.DisplayName,
		m.config.JoinToken,
		m.config.EnableAudio,
		m.config.EnableVideo,
	)
	if err != nil {
		sdk.Destroy()
		return fmt.Errorf("failed to join meeting: %w", err)
	}
	m.meetingHandle = meeting

	// Register this instance for callback routing
	registerMeetingHandle(meeting.handle, m)

	// Set audio callback if enabled
	if m.config.EnableAudio {
		if err := meeting.SetAudioCallback(); err != nil {
			meeting.Destroy()
			sdk.Destroy()
			return fmt.Errorf("failed to set audio callback: %w", err)
		}
	}

	log.Infof("Successfully joined meeting: %s", m.meetingID)
	return nil
}

// processAudioFrames processes incoming audio frames and forwards them to the bus
func (m *MeetingInstance) processAudioFrames() {

	for {
		select {
		case frame := <-m.audioChannel:
			// Update statistics
			m.stats.FramesReceived++
			m.stats.BytesReceived += uint64(len(frame.Data))
			m.stats.LastFrameTime = time.Now()

			if m.audioBus != nil {
				if !m.audioBus.Publish(m.meetingID, frame) {
					m.stats.FramesDropped++
					log.Warnf("Dropped audio frame for meeting %s", m.meetingID)
				}
			}

		case <-m.stopChan:
			log.Debugf("Stopping audio frame processor for meeting: %s", m.meetingID)
			return
		}
	}
}

// Stop stops the meeting instance and leaves the meeting
func (m *MeetingInstance) Stop() error {
	if m.stopped {
		return nil
	}

	log.Infof("Stopping meeting instance for meeting ID: %s", m.meetingID)

	// Stop audio frame processor
	close(m.stopChan)
	m.stopped = true

	// Stop SDK event loop
	StopLoop()

	// Clean up SDK resources on OS thread
	if m.osThread != nil {
		m.osThread.Execute(func() {
			if m.meetingHandle != nil {
				m.meetingHandle.Destroy()
				m.meetingHandle = nil
			}
			if m.sdkHandle != nil {
				m.sdkHandle.Destroy()
				m.sdkHandle = nil
			}
		})

		m.osThread.Stop()
	}

	// Drain audio channel
	go func() {
		for range m.audioChannel {
		}
	}()

	log.Infof("Meeting instance stopped: %s", m.meetingID)

	return nil
}
