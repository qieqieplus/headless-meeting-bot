package manifest

// trackState manages the lifecycle of a single media track (audio or video).

// It maintains a list of finished segments and optionally an open segment being recorded.
type trackState struct {
	format AudioFormat // Only used for audio tracks

	segments       []MediaSegment
	currentSegment *MediaSegment

	// pendingCloseTs stores the timestamp for a close request that arrived
	// before the segment was created. This handles the race condition where
	// a mute/share-stop event arrives before the file creation event.
	pendingCloseTs int64 // 0 means no pending close
}

// startSegment begins a new segment for this track.
// If a segment is already open, this is a no-op.
// If the last segment has the same path, this is also a no-op to prevent duplicates.
// If there's a pending close, the segment is immediately closed with the pending timestamp.
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

	// If there's a pending close, immediately close this segment
	if t.pendingCloseTs != 0 {
		t.currentSegment.EndMS = t.pendingCloseTs - t0UnixMs
		t.segments = append(t.segments, *t.currentSegment)
		t.currentSegment = nil
		t.pendingCloseTs = 0
	}
}

// closeSegment closes the current segment using an absolute end timestamp.
// The timestamp is converted to relative time before storing.
// If no segment is currently open, stores the timestamp as a pending close.
func (t *trackState) closeSegment(endUnixMs int64, t0UnixMs int64) {
	if t.currentSegment == nil {
		// No segment to close - store as pending close for when startSegment is called
		t.pendingCloseTs = endUnixMs
		return
	}
	t.currentSegment.EndMS = endUnixMs - t0UnixMs
	t.segments = append(t.segments, *t.currentSegment)
	t.currentSegment = nil
	t.pendingCloseTs = 0 // Clear any pending close
}

// hasOpenSegment returns true if there is currently an open segment.
func (t *trackState) hasOpenSegment() bool {
	return t.currentSegment != nil
}
