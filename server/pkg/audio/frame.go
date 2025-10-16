package audio

import (
	"encoding/binary"
)

// AudioType represents the type of audio data
type AudioType uint64

const (
	AudioTypeMixed  AudioType = 0 // ZOOM_AUDIO_TYPE_MIXED
	AudioTypeOneWay AudioType = 1 // ZOOM_AUDIO_TYPE_ONE_WAY
	AudioTypeShare  AudioType = 2 // ZOOM_AUDIO_TYPE_SHARE
)

func (t AudioType) String() string {
	switch t {
	case AudioTypeMixed:
		return "mixed"
	case AudioTypeOneWay:
		return "one_way"
	case AudioTypeShare:
		return "share"
	default:
		return "unknown"
	}
}

// AudioFrame represents a single audio frame from the Zoom SDK
type AudioFrame struct {
	Type   AudioType // Audio type
	UserID uint64    // Speaker/source identifier
	Data   []byte    // PCM audio data (S16LE)
}

var BinaryFrameHeaderSize = 2 * binary.Size(uint64(0)) // Type + UserID

// Encode serializes the frame for WebSocket transmission.
func (f *AudioFrame) Encode() []byte {
	buf := make([]byte, BinaryFrameHeaderSize+len(f.Data))
	binary.LittleEndian.PutUint64(buf[0:8], uint64(f.Type))
	binary.LittleEndian.PutUint64(buf[8:16], f.UserID)
	copy(buf[BinaryFrameHeaderSize:], f.Data)
	return buf
}
