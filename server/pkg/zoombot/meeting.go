package zoombot

import (
	"fmt"
	"sync"
	"time"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/stream"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/zoombot/native"
)

// MeetingInstance represents a single meeting session
type MeetingInstance struct {
	meetingID string
	config    *MeetingConfig

	sdkHandle     *native.SDKHandle
	meetingHandle *native.MeetingHandle

	// OS thread for SDK operations
	osThread *native.OSThread

	// Audio bus for publishing frames
	audioBus *stream.AudioBus

	eventsBus *stream.EventBus

	videoBus *stream.VideoBus

	stopped bool

	lastError error

	statistics   MeetingStatistics
	statisticsMu sync.Mutex

	statusMu     sync.RWMutex
	status       MeetingStatus
	statusDetail int

	users   map[uint64]stream.UserInfo
	usersMu sync.RWMutex
}

// NewMeetingInstance creates a new meeting instance
func NewMeetingInstance(config *MeetingConfig, audioBus *stream.AudioBus, eventsBus *stream.EventBus, videoBus *stream.VideoBus) *MeetingInstance {
	return &MeetingInstance{
		meetingID: config.MeetingID,
		config:    config,
		audioBus:  audioBus,
		eventsBus: eventsBus,
		videoBus:  videoBus,
		statistics: MeetingStatistics{
			StartTime: time.Now(),
		},
		status: StatusIdle,
		users:  make(map[uint64]stream.UserInfo),
	}
}

// GetStatus returns the current meeting status from Zoom SDK
func (m *MeetingInstance) GetStatus() MeetingStatus {
	m.statusMu.RLock()
	defer m.statusMu.RUnlock()
	return m.status
}

// GetUsers returns a copy of all users in the meeting.
func (m *MeetingInstance) GetUsers() []stream.UserInfo {
	m.usersMu.RLock()
	defer m.usersMu.RUnlock()
	users := make([]stream.UserInfo, 0, len(m.users))
	for _, u := range m.users {
		users = append(users, u)
	}
	return users
}

// GetStatistics returns meeting statistics
func (m *MeetingInstance) GetStatistics() MeetingStatistics {
	m.statisticsMu.Lock()
	defer m.statisticsMu.Unlock()
	return m.statistics
}

// GetStatusInfo returns a snapshot of the meeting status.
func (m *MeetingInstance) GetStatusInfo() StatusInfo {
	m.statusMu.RLock()
	status := m.status
	detail := m.statusDetail
	m.statusMu.RUnlock()

	info := StatusInfo{
		State:  status.String(),
		Detail: detail,
	}

	if m.lastError != nil {
		info.Error = m.lastError.Error()
	}

	return info
}

// Start starts the meeting instance and joins the meeting
func (m *MeetingInstance) Start() error {
	if m.GetStatus() != StatusIdle {
		return fmt.Errorf("meeting is not in idle state")
	}

	log.Infof("Starting meeting instance for meeting ID: %s", m.meetingID)
	m.HandleStatusChange(StatusConnecting, 0)

	m.osThread = native.NewOSThread()
	m.osThread.Start()

	var joinError error
	m.osThread.Execute(func() {
		joinError = m.joinMeeting()
	})

	if joinError != nil {
		m.HandleStatusChange(StatusIdle, 0)
		m.lastError = joinError
		return joinError
	}

	go func() {
		m.osThread.Execute(func() {
			native.RunLoop()
		})
	}()

	return nil
}

// joinMeeting performs the actual meeting join operation
func (m *MeetingInstance) joinMeeting() error {
	sdk, err := native.CreateSDK(m.config.SDKKey, m.config.SDKSecret)
	if err != nil {
		return fmt.Errorf("failed to create SDK: %w", err)
	}
	m.sdkHandle = sdk

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
	meeting.RegisterEventSink(m)

	if err := meeting.SetStatusCallback(); err != nil {
		meeting.Destroy()
		sdk.Destroy()
		return fmt.Errorf("failed to set status callback: %w", err)
	}

	if err := meeting.SetUserStatusCallback(); err != nil {
		meeting.Destroy()
		sdk.Destroy()
		return fmt.Errorf("failed to set user status callback: %w", err)
	}

	if m.config.EnableAudio {
		if err := meeting.SetAudioCallback(); err != nil {
			meeting.Destroy()
			sdk.Destroy()
			return fmt.Errorf("failed to set audio callback: %w", err)
		}
	}

	if m.config.EnableVideo {
		hlsConfig := &native.HlsVideoConfig{
			Width:            0,    // auto-detect
			Height:           0,    // auto-detect
			FPS:              10,   // auto-detect (matches demo.c defaults)
			BitrateKbps:      3000, // 3 Mbps
			Encoder:          "auto",
			Preset:           "fast", // match demo.c defaults
			HlsPrefix:        "",
			AudioSampleRate:  32000, // 32 kHz
			AudioChannels:    1,     // mono (matches demo.c defaults)
			AudioBitrateKbps: 128,   // 128 kbps
		}
		if err := meeting.SetHlsVideoCallback(hlsConfig); err != nil {
			meeting.Destroy()
			sdk.Destroy()
			return fmt.Errorf("failed to set HLS video callback: %w", err)
		}
	}

	log.Infof("Successfully joined meeting: %s", m.meetingID)
	return nil
}

// Stop stops the meeting instance and leaves the meeting
func (m *MeetingInstance) Stop() error {
	m.statusMu.Lock()
	if m.stopped {
		m.statusMu.Unlock()
		return nil
	}
	m.stopped = true
	m.statusMu.Unlock()

	log.Infof("Stopping meeting instance for meeting ID: %s", m.meetingID)

	if m.osThread != nil {
		// Request the GLib main loop to stop.
		native.StopLoop()

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

	log.Infof("Meeting instance stopped: %s", m.meetingID)

	m.HandleStatusChange(StatusIdle, 0)

	return nil
}
