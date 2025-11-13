package zoombot

import (
	"fmt"
	"time"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/stream"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/zoombot/native"
)

// HandleStatusChange handles status updates from the Zoom SDK bridge
func (m *MeetingInstance) HandleStatusChange(status MeetingStatus, detail int) {
	m.statusMu.Lock()
	m.status = status
	m.statusDetail = detail
	m.statusMu.Unlock()

	// Publish status event to events bus
	if m.eventsBus != nil {
		event := stream.NewMeetingStatusEvent(m.meetingID, status.String(), detail)
		m.eventsBus.Publish(m.meetingID, event)
	}

	switch status {
	case StatusFailed:
		m.lastError = fmt.Errorf("meeting failed with detail code %d", detail)
		// Trigger cleanup asynchronously to avoid blocking the SDK callback
		go func() {
			log.Infof("Meeting %s failed, initiating cleanup", m.meetingID)
			_ = m.Stop()
		}()
	case StatusEnded:
		// Meeting ended naturally, trigger cleanup
		go func() {
			log.Infof("Meeting %s ended, initiating cleanup", m.meetingID)
			_ = m.Stop()
		}()
	default:
		// clear failure state on recovery
		if status == StatusInMeeting || status == StatusConnecting {
			m.lastError = nil
		}
	}
}

// HandleUserStatusEvent handles user status events from the Zoom SDK bridge
func (m *MeetingInstance) HandleUserStatusEvent(event *UserStatusEvent) {
	if event == nil {
		return
	}

	// Convert to events package format
	var eventType stream.UserEventType
	switch event.EventType {
	case UserEventSnapshot:
		eventType = stream.UserEventSnapshot
	case UserEventJoined:
		eventType = stream.UserEventJoined
	case UserEventLeft:
		eventType = stream.UserEventLeft
	case UserEventAudioMuted:
		eventType = stream.UserEventAudioMuted
	case UserEventAudioUnmuted:
		eventType = stream.UserEventAudioUnmuted
	case UserEventVideoOn:
		eventType = stream.UserEventVideoOn
	case UserEventVideoOff:
		eventType = stream.UserEventVideoOff
	case UserEventShareStarted:
		eventType = stream.UserEventShareStarted
	case UserEventShareStopped:
		eventType = stream.UserEventShareStopped
	default:
		return
	}

	userInfo := stream.UserInfo{
		ID:    event.UserID,
		Name:  event.Name,
		Audio: event.Audio,
		Video: event.Video,
		Share: event.Share,
	}

	// Update internal user cache
	m.usersMu.Lock()
	switch event.EventType {
	case UserEventLeft:
		delete(m.users, event.UserID)
	default:
		// Covers Joined, Snapshot, and all status changes
		m.users[event.UserID] = userInfo
	}
	m.usersMu.Unlock()

	// Publish to events bus
	if m.eventsBus != nil {
		evt := stream.NewUserEvent(m.meetingID, eventType, userInfo, int64(event.Timestamp))
		m.eventsBus.Publish(m.meetingID, evt)
	}
}

// OnAudio implements native.EventSink
func (m *MeetingInstance) OnAudio(data []byte, audioType int, nodeID uint64) {
	// Copy C memory into pooled Go buffer
	frame := stream.NewAudioEvent(stream.AudioType(audioType), nodeID, data)

	// Update statistics
	m.statisticsMu.Lock()
	m.statistics.AudioFramesReceived++
	m.statistics.AudioBytesReceived += uint64(len(frame.Data))
	m.statistics.LastAudioSampleTime = time.Now()
	m.statisticsMu.Unlock()

	// Publish directly to bus; release if nobody received
	if m.audioBus == nil || !stream.PublishOrRelease(m.meetingID, m.audioBus, frame) {
		m.statisticsMu.Lock()
		m.statistics.AudioFramesDropped++
		m.statisticsMu.Unlock()
		log.Warnf("Dropped audio frame for meeting %s", m.meetingID)
	}
}

// OnMeetingStatusChanged implements native.EventSink
func (m *MeetingInstance) OnMeetingStatusChanged(status native.MeetingStatus, detail int) {
	m.HandleStatusChange(MeetingStatus(status), detail)
}

// OnUserStatusEvent implements native.EventSink
func (m *MeetingInstance) OnUserStatusEvent(event *native.UserStatusEvent) {
	if event == nil {
		return
	}
	converted := &UserStatusEvent{
		EventType: UserEventType(event.EventType),
		UserID:    event.UserID,
		Name:      event.Name,
		Audio:     event.Audio,
		Video:     event.Video,
		Share:     event.Share,
		Timestamp: event.Timestamp,
	}

	m.HandleUserStatusEvent(converted)
}

// OnHlsFile implements native.EventSink
func (m *MeetingInstance) OnHlsFile(filename string, data []byte, isPlaylist bool, sequence uint64) {
	if m.videoBus == nil {
		return
	}

	// Update video statistics
	m.statisticsMu.Lock()
	m.statistics.VideoFilesReceived++
	if !isPlaylist {
		m.statistics.VideoBytesReceived += uint64(len(data))
	}
	m.statistics.LastVideoFileTime = time.Now()
	m.statisticsMu.Unlock()

	// Debug: log incoming HLS file from native bridge
	log.Infof("[video] HLS file received: meeting=%s file=%s playlist=%t seq=%d len=%d",
		m.meetingID, filename, isPlaylist, sequence, len(data))

	var event *stream.FileEvent
	if isPlaylist {
		event = stream.NewVideoPlaylistEvent(m.meetingID, filename, string(data))
	} else {
		event = stream.NewVideoFileEvent(m.meetingID, filename, data, sequence)
	}

	if !stream.PublishOrRelease(m.meetingID, m.videoBus, event) {
		m.statisticsMu.Lock()
		m.statistics.VideoFilesDropped++
		m.statisticsMu.Unlock()
		log.Warnf("[video] published HLS file but no subscriber received it: meeting=%s file=%s (subs=%d)", m.meetingID, filename, m.videoBus.GetSubscriberCount())
	}
}
