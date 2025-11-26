package manifest

// Package manifest defines the shared data shapes for the meeting manifest API.
// The structures mirror the JSON contract expected by downstream recorders or
// players. All timestamps and durations are expressed in milliseconds relative
// to t0 (the timeline base set by the first media frame). Paths are treated as
// logical identifiers only—no file I/O occurs in the Go server.

// MediaSegment represents a file segment with relative timeline positions.
// Each segment corresponds to one physical file on disk.
type MediaSegment struct {
	Path    string `json:"path"`
	StartMS int64  `json:"start_ms"` // Start time relative to t0 (ms)
	EndMS   int64  `json:"end_ms"`   // End time relative to t0 (ms)
}

// AudioTrackType enumerates audio track flavors recorded by the headless bot.
type AudioTrackType string

const (
	AudioTrackMixed AudioTrackType = "mixed"
	AudioTrackUser  AudioTrackType = "user"
	AudioTrackShare AudioTrackType = "share"
)

// AudioFormat describes the static PCM characteristics for a track.
type AudioFormat struct {
	Encoding   string `json:"encoding"`    // e.g. "S16LE"
	SampleRate int    `json:"sample_rate"` // Hz
	Channels   int    `json:"channels"`
}

// AudioTrack represents one logical audio stream with potentially multiple file segments.
// There is one AudioTrack per (type, userID) combination.
type AudioTrack struct {
	Type     AudioTrackType `json:"type"`
	UserID   uint64         `json:"user_id"`
	Format   AudioFormat    `json:"format,omitempty"`
	Segments []MediaSegment `json:"segments"`
}

// VideoTrackType enumerates the supported video track categories.
type VideoTrackType string

const (
	VideoTrackShare VideoTrackType = "share"
	VideoTrackUser  VideoTrackType = "user"
)

// VideoTrack represents one logical video stream with potentially multiple file segments.
// There is one VideoTrack per (type, userID) combination.
type VideoTrack struct {
	Type     VideoTrackType `json:"type"`
	UserID   uint64         `json:"user_id"`
	Segments []MediaSegment `json:"segments"`
}

// ManifestEvent captures timeline-aligned meeting metadata.
type ManifestEvent struct {
	Type      string `json:"type"`
	UserID    string `json:"user_id,omitempty"`
	Event     string `json:"event"`
	Timestamp int64  `json:"timestamp"` // Media timeline timestamp (ms)
	WallTs    int64  `json:"wall_ts"`   // Absolute unix ms for redundancy
}

// MeetingManifest is the top-level payload returned by the manifest API.
type MeetingManifest struct {
	MeetingID  string          `json:"meeting_id"`
	T0UnixMs   int64           `json:"t0_unix_ms"`
	DurationMS int64           `json:"duration_ms"`
	Audio      []AudioTrack    `json:"audio"`
	Video      []VideoTrack    `json:"video"`
	Events     []ManifestEvent `json:"events"`
}

type audioTrackKey struct {
	Type   AudioTrackType
	UserID uint64
}

type videoTrackKey struct {
	Type   VideoTrackType
	UserID uint64
}
