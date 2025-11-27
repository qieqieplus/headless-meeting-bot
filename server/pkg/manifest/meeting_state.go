package manifest

import (
	"sort"
	"strconv"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/stream"
)

// meetingState stores manifest data for a single meeting.
type meetingState struct {
	meetingID string
	t0UnixMs  int64
	closed    bool

	lastMediaTs int64

	audioTracks map[audioTrackKey]*trackState
	videoTracks map[videoTrackKey]*trackState
	events      []ManifestEvent
}

func newMeetingState(meetingID string) *meetingState {
	return &meetingState{
		meetingID:   meetingID,
		audioTracks: make(map[audioTrackKey]*trackState),
		videoTracks: make(map[videoTrackKey]*trackState),
	}
}

// setT0 sets the meeting start time (t0) exactly once.
// Subsequent calls are ignored to maintain consistency.
// The t0 value should be provided by ProcessManager from the first event with valid timing.
func (s *meetingState) setT0(t0UnixMs int64) {
	if s.t0UnixMs == 0 {
		s.t0UnixMs = t0UnixMs
	}
}

// hasT0 returns true if the meeting start time has been set.
func (s *meetingState) hasT0() bool {
	return s.t0UnixMs != 0
}

func (s *meetingState) ensureAudioTrack(key audioTrackKey, format AudioFormat) *trackState {
	if track, ok := s.audioTracks[key]; ok {
		return track
	}

	track := &trackState{
		format: format,
	}
	s.audioTracks[key] = track
	return track
}

func (s *meetingState) ensureVideoTrack(key videoTrackKey) *trackState {
	if track, ok := s.videoTracks[key]; ok {
		return track
	}

	track := &trackState{}
	s.videoTracks[key] = track
	return track
}

func (s *meetingState) recordEvent(ev *stream.Event) {
	manifestEvent := ManifestEvent{
		Type: string(ev.Type),
	}

	if ev.User.ID != 0 {
		manifestEvent.UserID = strconv.FormatUint(ev.User.ID, 10)
	}

	if ev.Event != "" {
		manifestEvent.Event = string(ev.Event)
	} else if ev.Status != "" {
		manifestEvent.Event = ev.Status
	}

	manifestEvent.WallTs = ev.WallTs
	manifestEvent.Timestamp = s.resolveMediaTs(ev)

	s.events = append(s.events, manifestEvent)
	s.bumpDuration(manifestEvent.Timestamp)
}

func (s *meetingState) resolveMediaTs(ev *stream.Event) int64 {
	if ev.MediaTs != 0 {
		return ev.MediaTs
	}
	if s.t0UnixMs == 0 || ev.WallTs == 0 {
		return 0
	}
	return ev.WallTs - s.t0UnixMs
}

func (s *meetingState) bumpDuration(mediaTs int64) {
	if mediaTs > s.lastMediaTs {
		s.lastMediaTs = mediaTs
	}
}

func (s *meetingState) closeAllOpenAudio() {
	for _, track := range s.audioTracks {
		if track.hasOpenSegment() {
			track.closeSegment(s.t0UnixMs+s.lastMediaTs, s.t0UnixMs)
		}
	}
}

func (s *meetingState) closeAllOpenVideo() {
	for _, track := range s.videoTracks {
		if track.hasOpenSegment() {
			track.closeSegment(s.t0UnixMs+s.lastMediaTs, s.t0UnixMs)
		}
	}
}

func (s *meetingState) snapshot() *MeetingManifest {
	audio := make([]AudioTrack, 0, len(s.audioTracks))
	for key, track := range s.audioTracks {
		// Clone segments
		segments := make([]MediaSegment, len(track.segments))
		copy(segments, track.segments)

		// Include open segment if present
		if track.currentSegment != nil {
			segments = append(segments, *track.currentSegment)
		}

		audio = append(audio, AudioTrack{
			Type:     key.Type,
			UserID:   key.UserID,
			Format:   track.format,
			Segments: segments,
		})
	}

	sort.Slice(audio, func(i, j int) bool {
		if audio[i].Type != audio[j].Type {
			return audio[i].Type < audio[j].Type
		}
		if audio[i].UserID != audio[j].UserID {
			return audio[i].UserID < audio[j].UserID
		}
		// Sort by start time of first segment
		startI := int64(0)
		if len(audio[i].Segments) > 0 {
			startI = audio[i].Segments[0].StartMS
		}
		startJ := int64(0)
		if len(audio[j].Segments) > 0 {
			startJ = audio[j].Segments[0].StartMS
		}
		return startI < startJ
	})

	video := make([]VideoTrack, 0, len(s.videoTracks))
	for key, track := range s.videoTracks {
		// Clone segments
		segments := make([]MediaSegment, len(track.segments))
		copy(segments, track.segments)

		// Include open segment if present
		if track.currentSegment != nil {
			segments = append(segments, *track.currentSegment)
		}

		video = append(video, VideoTrack{
			Type:     key.Type,
			UserID:   key.UserID,
			Segments: segments,
		})
	}

	sort.Slice(video, func(i, j int) bool {
		if video[i].Type != video[j].Type {
			return video[i].Type < video[j].Type
		}
		if video[i].UserID != video[j].UserID {
			return video[i].UserID < video[j].UserID
		}
		// Sort by start time of first segment
		startI := int64(0)
		if len(video[i].Segments) > 0 {
			startI = video[i].Segments[0].StartMS
		}
		startJ := int64(0)
		if len(video[j].Segments) > 0 {
			startJ = video[j].Segments[0].StartMS
		}
		return startI < startJ
	})

	events := make([]ManifestEvent, len(s.events))
	copy(events, s.events)

	return &MeetingManifest{
		MeetingID:  s.meetingID,
		T0UnixMs:   s.t0UnixMs,
		DurationMS: s.lastMediaTs,
		Audio:      audio,
		Video:      video,
		Events:     events,
	}
}
