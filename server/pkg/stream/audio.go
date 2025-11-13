package stream

import (
	"encoding/binary"
	"sync"
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

// AudioEvent represents a single audio event from the Zoom SDK
type AudioEvent struct {
	Type   AudioType // Audio type
	UserID uint64    // Speaker/source identifier
	Data   []byte    // PCM audio data (S16LE) - pooled buffer
}

var AudioEventHeaderSize = 2 * binary.Size(uint64(0)) // Type + UserID

// NewAudioEvent creates a new audio event by copying data from C memory into a pooled buffer.
func NewAudioEvent(audioType AudioType, userID uint64, cData []byte) *AudioEvent {
	buf := AudioBufferPool.Get(len(cData))
	copy(buf, cData)
	return &AudioEvent{
		Type:   audioType,
		UserID: userID,
		Data:   buf,
	}
}

// Encode serializes the frame for WebSocket transmission.
func (f *AudioEvent) Encode() []byte {
	buf := make([]byte, AudioEventHeaderSize+len(f.Data))
	binary.LittleEndian.PutUint64(buf[0:8], uint64(f.Type))
	binary.LittleEndian.PutUint64(buf[8:16], f.UserID)
	copy(buf[AudioEventHeaderSize:], f.Data)
	return buf
}

// Release returns the underlying buffer to the pool. Safe to call once.
func (f *AudioEvent) Release() {
	if f != nil && f.Data != nil {
		AudioBufferPool.Put(f.Data)
		f.Data = nil
	}
}

// AudioSubscriber represents a client subscribed to audio streams with filtering.
type AudioSubscriber struct {
	*Subscriber[*AudioEvent]
	audioTypes map[AudioType]bool
	userIDs    map[uint64]bool
	mutex      sync.RWMutex
}

// NewAudioSubscriber creates a new audio subscriber with custom filtering.
func NewAudioSubscriber(id string, bufferSize int) *AudioSubscriber {
	s := &AudioSubscriber{
		Subscriber: NewSubscriber[*AudioEvent](id, bufferSize),
		audioTypes: make(map[AudioType]bool),
		userIDs:    make(map[uint64]bool),
	}
	s.SetFilter(s.matches)
	return s
}

// matches implements the filter predicate for audio frames.
func (s *AudioSubscriber) matches(frame *AudioEvent) bool {
	s.mutex.RLock()
	defer s.mutex.RUnlock()

	if len(s.audioTypes) > 0 && !s.audioTypes[frame.Type] {
		return false
	}

	if len(s.userIDs) > 0 && !s.userIDs[frame.UserID] {
		return false
	}

	return true
}

// SetAudioTypeFilter sets the audio type filter.
func (s *AudioSubscriber) SetAudioTypeFilter(audioTypes []AudioType) {
	s.mutex.Lock()
	s.audioTypes = make(map[AudioType]bool)
	for _, t := range audioTypes {
		s.audioTypes[t] = true
	}
	s.mutex.Unlock()
}

// SetUserIDFilter sets the user ID filter.
func (s *AudioSubscriber) SetUserIDFilter(userIDs []uint64) {
	s.mutex.Lock()
	s.userIDs = make(map[uint64]bool)
	for _, id := range userIDs {
		s.userIDs[id] = true
	}
	s.mutex.Unlock()
}

// AudioBus manages audio frame distribution to subscribers.
type AudioBus struct {
	*Bus[*AudioEvent]
}

// NewAudioBus creates a new audio bus.
func NewAudioBus() *AudioBus {
	return &AudioBus{
		Bus: NewBus[*AudioEvent]("audio"),
	}
}

// Subscribe adds an audio subscriber to the bus.
func (b *AudioBus) Subscribe(subscriber *AudioSubscriber) {
	b.Bus.Subscribe(subscriber.Subscriber)
}

// Unsubscribe removes an audio subscriber from the bus.
func (b *AudioBus) Unsubscribe(subscriberID string) {
	b.Bus.Unsubscribe(subscriberID)
}
