package audio

import (
	"sync"
	"time"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
)

// Subscriber represents a client subscribed to audio streams
type Subscriber struct {
	ID           string
	MeetingID    string             // Filter by meeting ID (empty for all meetings)
	AudioTypes   map[AudioType]bool // Filter by audio types
	UserIDs      map[uint64]bool    // Filter by node IDs (empty for all nodes)
	Channel      chan *AudioFrame
	LastActivity time.Time
	connected    bool
	mutex        sync.RWMutex
}

// NewSubscriber creates a new audio subscriber
func NewSubscriber(id string, bufferSize int) *Subscriber {
	return &Subscriber{
		ID:           id,
		AudioTypes:   make(map[AudioType]bool),
		UserIDs:      make(map[uint64]bool),
		Channel:      make(chan *AudioFrame, bufferSize),
		LastActivity: time.Now(),
		connected:    true,
	}
}

// SetMeetingFilter sets the meeting ID filter
func (s *Subscriber) SetMeetingFilter(meetingID string) {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	s.MeetingID = meetingID
}

// SetAudioTypeFilter sets the audio type filter
func (s *Subscriber) SetAudioTypeFilter(audioTypes []AudioType) {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	s.AudioTypes = make(map[AudioType]bool)
	for _, t := range audioTypes {
		s.AudioTypes[t] = true
	}
}

// SetUserIDFilter sets the node ID filter
func (s *Subscriber) SetUserIDFilter(userIDs []uint64) {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	s.UserIDs = make(map[uint64]bool)
	for _, id := range userIDs {
		s.UserIDs[id] = true
	}
}

// ShouldReceive checks if the subscriber should receive this frame
func (s *Subscriber) ShouldReceive(meetingID string, frame *AudioFrame) bool {
	s.mutex.RLock()
	defer s.mutex.RUnlock()

	if !s.connected {
		return false
	}

	// Check meeting ID filter
	if s.MeetingID != "" && s.MeetingID != meetingID {
		return false
	}

	// Check audio type filter
	if len(s.AudioTypes) > 0 && !s.AudioTypes[frame.Type] {
		return false
	}

	// Check node ID filter
	if len(s.UserIDs) > 0 && !s.UserIDs[frame.UserID] {
		return false
	}

	return true
}

// Send sends a frame to the subscriber (non-blocking)
func (s *Subscriber) Send(frame *AudioFrame) bool {
	s.mutex.Lock()
	defer s.mutex.Unlock()

	if !s.connected {
		return false
	}

	select {
	case s.Channel <- frame:
		s.LastActivity = time.Now()
		return true
	default:
		// Channel is full, drop the frame
		log.Warnf("Dropping frame for subscriber %s (channel full)", s.ID)
		return false
	}
}

// Close closes the subscriber
func (s *Subscriber) Close() {
	s.mutex.Lock()
	defer s.mutex.Unlock()

	if s.connected {
		s.connected = false
		close(s.Channel)
	}
}

// IsConnected returns whether the subscriber is connected
func (s *Subscriber) IsConnected() bool {
	s.mutex.RLock()
	defer s.mutex.RUnlock()
	return s.connected
}

// Bus manages audio frame distribution to subscribers
type Bus struct {
	subscribers map[string]*Subscriber
	mutex       sync.RWMutex
	stats       BusStats
}

// BusStats holds statistics for the audio bus
type BusStats struct {
	TotalFrames       uint64
	DroppedFrames     uint64
	ActiveSubscribers int
	LastFrameTime     time.Time
}

// NewBus creates a new audio bus
func NewBus() *Bus {
	return &Bus{
		subscribers: make(map[string]*Subscriber),
	}
}

// Subscribe adds a new subscriber to the bus
func (b *Bus) Subscribe(subscriber *Subscriber) {
	b.mutex.Lock()
	defer b.mutex.Unlock()

	b.subscribers[subscriber.ID] = subscriber
	b.stats.ActiveSubscribers = len(b.subscribers)

	log.Infof("Added subscriber: %s (total: %d)", subscriber.ID, b.stats.ActiveSubscribers)
}

// Unsubscribe removes a subscriber from the bus
func (b *Bus) Unsubscribe(subscriberID string) {
	b.mutex.Lock()
	defer b.mutex.Unlock()

	if subscriber, exists := b.subscribers[subscriberID]; exists {
		subscriber.Close()
		delete(b.subscribers, subscriberID)
		b.stats.ActiveSubscribers = len(b.subscribers)

		log.Infof("Removed subscriber: %s (total: %d)", subscriberID, b.stats.ActiveSubscribers)
	}
}

// Publish publishes an audio frame to all matching subscribers
func (b *Bus) Publish(meetingID string, frame *AudioFrame) bool {
	b.mutex.RLock()
	subscribers := make([]*Subscriber, 0, len(b.subscribers))
	for _, sub := range b.subscribers {
		if sub.ShouldReceive(meetingID, frame) {
			subscribers = append(subscribers, sub)
		}
	}
	b.mutex.RUnlock()

	// Update stats
	b.stats.TotalFrames++
	b.stats.LastFrameTime = time.Now()

	if len(subscribers) == 0 {
		return true // No subscribers, but not an error
	}

	// Send to all matching subscribers
	sent := 0
	for _, subscriber := range subscribers {
		// Skip disconnected subscribers silently
		if !subscriber.IsConnected() {
			continue
		}

		if subscriber.Send(frame) {
			sent++
		} else {
			b.stats.DroppedFrames++
		}
	}

	return sent > 0
}

// GetStats returns bus statistics
func (b *Bus) GetStats() BusStats {
	b.mutex.RLock()
	defer b.mutex.RUnlock()

	stats := b.stats
	stats.ActiveSubscribers = len(b.subscribers)
	return stats
}

// GetSubscriber returns a subscriber by ID
func (b *Bus) GetSubscriber(subscriberID string) (*Subscriber, bool) {
	b.mutex.RLock()
	defer b.mutex.RUnlock()

	subscriber, exists := b.subscribers[subscriberID]
	return subscriber, exists
}

// GetSubscriberCount returns the number of active subscribers
func (b *Bus) GetSubscriberCount() int {
	b.mutex.RLock()
	defer b.mutex.RUnlock()
	return len(b.subscribers)
}

// CleanupInactiveSubscribers removes subscribers that haven't been active for a while
func (b *Bus) CleanupInactiveSubscribers(timeout time.Duration) int {
	b.mutex.Lock()
	defer b.mutex.Unlock()

	now := time.Now()
	removed := 0

	for id, subscriber := range b.subscribers {
		if !subscriber.IsConnected() || now.Sub(subscriber.LastActivity) > timeout {
			subscriber.Close()
			delete(b.subscribers, id)
			removed++
			log.Infof("Cleaned up inactive subscriber: %s", id)
		}
	}

	if removed > 0 {
		b.stats.ActiveSubscribers = len(b.subscribers)
		log.Infof("Cleaned up %d inactive subscribers (total: %d)", removed, b.stats.ActiveSubscribers)
	}

	return removed
}

// Shutdown closes all subscribers and shuts down the bus
func (b *Bus) Shutdown() {
	b.mutex.Lock()
	defer b.mutex.Unlock()

	log.Info("Shutting down audio bus")

	for id, subscriber := range b.subscribers {
		subscriber.Close()
		log.Debugf("Closed subscriber: %s", id)
	}

	b.subscribers = make(map[string]*Subscriber)
	b.stats.ActiveSubscribers = 0

	log.Info("Audio bus shutdown complete")
}
