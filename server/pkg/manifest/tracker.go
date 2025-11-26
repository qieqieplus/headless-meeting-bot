package manifest

import (
	"errors"
	"regexp"
	"strconv"
	"sync"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/stream"
)

// ErrMeetingNotFound is returned when manifest data is requested for an unknown meeting.
var ErrMeetingNotFound = errors.New("manifest: meeting not found")

// Tracker describes the contract required by the HTTP API and pipeline wiring.
type Tracker interface {
	OnMeetingStart(meetingID string, t0UnixMs int64)
	OnMeetingEnd(meetingID string)
	OnUserEvent(ev *stream.Event)
	OnAudioSegment(meetingID, filename string, trackType AudioTrackType, userID uint64)
	OnVideoSegment(meetingID, filename string, startMediaTs, durationMs int64, isShare bool, userID uint64)
	GetManifest(meetingID string) (*MeetingManifest, error)
}

// Options holds the configuration for the in-memory manager.
type Options struct {
	AudioFormat AudioFormat
}

// InMemoryTracker keeps per-meeting manifest state protected by a mutex.
type InMemoryTracker struct {
	mu          sync.RWMutex
	meetingMap  map[string]*meetingState
	audioFormat AudioFormat
}

// ParseTimestampFromFilename extracts the Unix timestamp (ms) from media filenames.
// Expected formats: "0_mixed_1763965414391.wav", "123_user_1763965444593.wav", "16778240_share_1763974041176.m3u8"
func ParseTimestampFromFilename(filename string) int64 {
	// Match pattern: underscore followed by 13 digits then file extension
	re := regexp.MustCompile(`_(\d{13})\.(wav|mp3|aac|m3u8)$`)
	matches := re.FindStringSubmatch(filename)
	if len(matches) >= 2 {
		ts, err := strconv.ParseInt(matches[1], 10, 64)
		if err == nil {
			return ts
		}
	}
	return 0
}

// NewInMemoryTracker builds a manager with sane defaults (32kHz S16LE mono).
func NewInMemoryTracker(opts Options) *InMemoryTracker {
	format := opts.AudioFormat
	if format.Encoding == "" {
		format = AudioFormat{
			Encoding:   "S16LE",
			SampleRate: 32000,
			Channels:   1,
		}
	}

	return &InMemoryTracker{
		meetingMap:  make(map[string]*meetingState),
		audioFormat: format,
	}
}

// OnMeetingStart is called when the bot joins a meeting.
// Note: t0 is now set from the first media file, not here.
func (m *InMemoryTracker) OnMeetingStart(meetingID string, t0UnixMs int64) {
	if meetingID == "" {
		return
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	// Just ensure the meeting state exists; t0 will be set by first media file
	m.ensureMeetingLocked(meetingID)
}

// OnMeetingEnd finalizes open segments but keeps the manifest available for lookup.
func (m *InMemoryTracker) OnMeetingEnd(meetingID string) {
	if meetingID == "" {
		return
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	state, ok := m.meetingMap[meetingID]
	if !ok {
		return
	}

	state.closeAllOpenAudio()
	state.closeAllOpenVideo()
	state.closed = true
}

// OnUserEvent records events and closes audio segments on mute.
func (m *InMemoryTracker) OnUserEvent(ev *stream.Event) {
	if ev == nil || ev.MeetingID == "" {
		return
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	state := m.ensureMeetingLocked(ev.MeetingID)
	state.recordEvent(ev)

	if ev.Type != stream.EventTypeUserEvent || ev.User.ID == 0 {
		return
	}

	// Handle audio mute: close the current segment
	if ev.Event == stream.UserEventAudioMuted {
		key := audioTrackKey{Type: AudioTrackUser, UserID: ev.User.ID}
		if track := state.audioTracks[key]; track != nil {
			wallTs := state.t0UnixMs + ev.MediaTs
			track.closeSegment(wallTs, state.t0UnixMs)
			state.bumpDuration(ev.MediaTs)
		}
	}

	// Handle share stopped: close the current video segment
	if ev.Event == stream.UserEventShareStopped {
		trackType := VideoTrackShare
		key := videoTrackKey{Type: trackType, UserID: ev.User.ID}
		if track := state.videoTracks[key]; track != nil {
			wallTs := state.t0UnixMs + ev.MediaTs
			track.closeSegment(wallTs, state.t0UnixMs)
			state.bumpDuration(ev.MediaTs)
		}
	}
}

// OnAudioSegment records a new audio file.
// The file timestamp becomes the segment start, and it remains open until mute or meeting end.
func (m *InMemoryTracker) OnAudioSegment(meetingID, filename string, trackType AudioTrackType, userID uint64) {
	if meetingID == "" || filename == "" {
		return
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	state := m.ensureMeetingLocked(meetingID)

	// Parse timestamp from filename
	fileTs := ParseTimestampFromFilename(filename)
	if fileTs == 0 {
		return
	}

	// Set t0 from first media file
	state.ensureT0(fileTs)

	// Get or create track
	key := audioTrackKey{Type: trackType, UserID: userID}
	track := state.ensureAudioTrack(key, m.audioFormat)

	// Start new segment
	track.startSegment(filename, fileTs, state.t0UnixMs)
}

// OnVideoSegment records a new video playlist file.
// The playlist timestamp becomes the segment start, and it remains open until share_stopped or meeting end.
// This works like OnAudioSegment - HLS playlists are continuous streams, not discrete segments.
func (m *InMemoryTracker) OnVideoSegment(meetingID, filename string, startMediaTs, durationMs int64, isShare bool, userID uint64) {
	if meetingID == "" || filename == "" {
		return
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	state := m.ensureMeetingLocked(meetingID)

	// Parse timestamp from filename to set t0 if not already set
	fileTs := ParseTimestampFromFilename(filename)
	if fileTs == 0 {
		return
	}

	// Set t0 from first media file
	state.ensureT0(fileTs)

	trackType := VideoTrackUser
	if isShare {
		trackType = VideoTrackShare
	}

	key := videoTrackKey{Type: trackType, UserID: userID}
	track := state.ensureVideoTrack(key)

	// Start new segment (startSegment already prevents duplicates)
	// Keep it open until share_stopped or meeting end
	track.startSegment(filename, fileTs, state.t0UnixMs)
}

// GetManifest returns a deep copy of the current manifest for a meeting.
func (m *InMemoryTracker) GetManifest(meetingID string) (*MeetingManifest, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	state, ok := m.meetingMap[meetingID]
	if !ok {
		return nil, ErrMeetingNotFound
	}
	return state.snapshot(), nil
}

func (m *InMemoryTracker) ensureMeetingLocked(meetingID string) *meetingState {
	if state, ok := m.meetingMap[meetingID]; ok {
		return state
	}

	state := newMeetingState(meetingID)
	m.meetingMap[meetingID] = state
	return state
}
