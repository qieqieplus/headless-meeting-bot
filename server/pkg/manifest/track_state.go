package manifest

// trackState manages the lifecycle of a single media track (audio or video).
// It maintains a list of finished segments and optionally an open segment being recorded.
type trackState struct {
	format AudioFormat // Only used for audio tracks

	segments       []MediaSegment
	currentSegment *MediaSegment
}

// startSegment begins a new segment for this track.
// If a segment is already open, this is a no-op.
// If the last segment has the same path, this is also a no-op to prevent duplicates.
func (t *trackState) startSegment(path string, startUnixMs int64, t0UnixMs int64) {
	if t.currentSegment != nil {
		return // Already have an open segment
	}
	// Check if the last segment has the same path to prevent duplicates
	if len(t.segments) > 0 && t.segments[len(t.segments)-1].Path == path {
		return // Duplicate segment with same path
	}
	t.currentSegment = &MediaSegment{
		Path:    path,
		StartMS: startUnixMs - t0UnixMs, // Convert to relative time
		EndMS:   0,                      // Will be set on close
	}
}

// closeSegment closes the current segment using an absolute end timestamp.
// The timestamp is converted to relative time before storing.
func (t *trackState) closeSegment(endUnixMs int64, t0UnixMs int64) {
	if t.currentSegment == nil {
		return
	}
	t.currentSegment.EndMS = endUnixMs - t0UnixMs
	t.segments = append(t.segments, *t.currentSegment)
	t.currentSegment = nil
}

// closeWithDuration closes the current segment using a duration in ms.
// This is useful for video tracks where HLS provides segment duration directly.
func (t *trackState) closeWithDuration(durationMs int64) {
	if t.currentSegment == nil {
		return
	}
	t.currentSegment.EndMS = t.currentSegment.StartMS + durationMs
	t.segments = append(t.segments, *t.currentSegment)
	t.currentSegment = nil
}

// hasOpenSegment returns true if there is currently an open segment.
func (t *trackState) hasOpenSegment() bool {
	return t.currentSegment != nil
}
