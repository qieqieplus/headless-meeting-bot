package manifest

import (
	"fmt"
	"regexp"
	"strconv"
	"strings"
)

// AudioFileInfo represents parsed audio filename metadata.
// Example filenames:
//   - "0_mixed_1763965414391.wav"
//   - "123_user_1763965444593.mp3"
//   - "456_user_1763965444593.aac"
type AudioFileInfo struct {
	UserID      uint64
	Type        AudioTrackType
	TimestampMs int64
	Extension   string
}

// VideoFileInfo represents parsed video filename metadata.
// Example filenames:
//   - "16778240_share_1763974041176.m3u8"
//   - "456_cam_1763974041176.m3u8"
//   - "789_user_1763974041176.m3u8"
type VideoFileInfo struct {
	UserID      uint64
	Type        VideoTrackType
	TimestampMs int64
	IsPlaylist  bool
}

var (
	// Matches filenames like: "0_mixed_1763965414391.wav" or "123_user_1763965444593.mp3"
	audioFilenameRegex = regexp.MustCompile(`^(\d+)_(mixed|user|share)_(\d{13})\.(wav|mp3|aac)$`)

	// Matches filenames like: "16778240_share_1763974041176.m3u8" or "456_cam_1763974041176.m3u8"
	videoFilenameRegex = regexp.MustCompile(`^(\d+)_(share|cam|user)_(\d{13})\.(m3u8|ts)$`)

	// Generic timestamp extractor for any media file
	timestampRegex = regexp.MustCompile(`_(\d{13})\.(wav|mp3|aac|m3u8|ts)$`)
)

// ParseAudioFilename parses an audio filename and extracts metadata.
// Returns an error if the filename doesn't match the expected format.
func ParseAudioFilename(filename string) (*AudioFileInfo, error) {
	matches := audioFilenameRegex.FindStringSubmatch(filename)
	if len(matches) != 5 {
		return nil, fmt.Errorf("invalid audio filename format: %s", filename)
	}

	userID, err := strconv.ParseUint(matches[1], 10, 64)
	if err != nil {
		return nil, fmt.Errorf("invalid user ID in filename %s: %w", filename, err)
	}

	timestamp, err := strconv.ParseInt(matches[3], 10, 64)
	if err != nil {
		return nil, fmt.Errorf("invalid timestamp in filename %s: %w", filename, err)
	}

	trackType := AudioTrackUser
	switch matches[2] {
	case "mixed":
		trackType = AudioTrackMixed
	case "share":
		trackType = AudioTrackShare
	case "user":
		trackType = AudioTrackUser
	default:
		return nil, fmt.Errorf("unknown audio track type: %s", matches[2])
	}

	return &AudioFileInfo{
		UserID:      userID,
		Type:        trackType,
		TimestampMs: timestamp,
		Extension:   matches[4],
	}, nil
}

// ParseVideoFilename parses a video filename and extracts metadata.
// Returns an error if the filename doesn't match the expected format.
func ParseVideoFilename(filename string) (*VideoFileInfo, error) {
	matches := videoFilenameRegex.FindStringSubmatch(filename)
	if len(matches) != 5 {
		return nil, fmt.Errorf("invalid video filename format: %s", filename)
	}

	userID, err := strconv.ParseUint(matches[1], 10, 64)
	if err != nil {
		return nil, fmt.Errorf("invalid user ID in filename %s: %w", filename, err)
	}

	timestamp, err := strconv.ParseInt(matches[3], 10, 64)
	if err != nil {
		return nil, fmt.Errorf("invalid timestamp in filename %s: %w", filename, err)
	}

	trackType := VideoTrackUser
	if matches[2] == "share" {
		trackType = VideoTrackShare
	}

	return &VideoFileInfo{
		UserID:      userID,
		Type:        trackType,
		TimestampMs: timestamp,
		IsPlaylist:  matches[4] == "m3u8",
	}, nil
}

// ParseTimestampFromFilename extracts the Unix timestamp (ms) from any media filename.
// This is a fallback for cases where you only need the timestamp.
// Returns 0 if no valid timestamp is found.
func ParseTimestampFromFilename(filename string) int64 {
	matches := timestampRegex.FindStringSubmatch(filename)
	if len(matches) >= 2 {
		ts, err := strconv.ParseInt(matches[1], 10, 64)
		if err == nil {
			return ts
		}
	}
	return 0
}

// IsVideoPlaylist returns true if the filename is a video playlist (.m3u8).
func IsVideoPlaylist(filename string) bool {
	return strings.HasSuffix(filename, ".m3u8")
}
