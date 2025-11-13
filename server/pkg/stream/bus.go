package stream

import (
	"sync"
	"time"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
)

// Filter is used to decide whether an item should be delivered to a subscriber.
type Filter[T any] func(item T) bool

// Subscriber represents a generic meeting-scoped subscriber for streamed items.
// It provides channel-based delivery with configurable filtering.
type Subscriber[T any] struct {
	id           string
	meetingID    string
	filter       Filter[T]
	channel      chan T
	lastActivity time.Time
	connected    bool
	mutex        sync.RWMutex
}

// NewSubscriber constructs a new subscriber with the provided buffer size.
func NewSubscriber[T any](id string, bufferSize int) *Subscriber[T] {
	return &Subscriber[T]{
		id:           id,
		channel:      make(chan T, bufferSize),
		lastActivity: time.Now(),
		connected:    true,
	}
}

// ID returns the subscriber identifier.
func (s *Subscriber[T]) ID() string {
	return s.id
}

// Channel exposes the subscriber channel.
func (s *Subscriber[T]) Channel() <-chan T {
	return s.channel
}

// SetMeetingFilter configures the meeting filter for the subscriber.
func (s *Subscriber[T]) SetMeetingFilter(meetingID string) {
	s.mutex.Lock()
	s.meetingID = meetingID
	s.mutex.Unlock()
}

// SetFilter assigns the predicate used to decide if an item should be delivered.
func (s *Subscriber[T]) SetFilter(filter Filter[T]) {
	s.mutex.Lock()
	s.filter = filter
	s.mutex.Unlock()
}

// ShouldReceive determines whether the item should be delivered to the subscriber.
func (s *Subscriber[T]) ShouldReceive(meetingID string, item T) bool {
	s.mutex.RLock()
	defer s.mutex.RUnlock()

	if !s.connected {
		return false
	}

	if s.meetingID != "" && s.meetingID != meetingID {
		return false
	}

	if s.filter != nil && !s.filter(item) {
		return false
	}

	return true
}

// Send pushes an item to the subscriber channel.
func (s *Subscriber[T]) Send(item T) bool {
	s.mutex.Lock()
	defer s.mutex.Unlock()

	if !s.connected {
		return false
	}

	select {
	case s.channel <- item:
		s.lastActivity = time.Now()
		return true
	default:
		log.Warnf("Dropping stream item for subscriber %s (channel full)", s.id)
		return false
	}
}

// Close marks the subscriber as disconnected and closes the underlying channel.
func (s *Subscriber[T]) Close() {
	s.mutex.Lock()
	defer s.mutex.Unlock()

	if s.connected {
		s.connected = false
		close(s.channel)
	}
}

// IsConnected returns whether the subscriber is still active.
func (s *Subscriber[T]) IsConnected() bool {
	s.mutex.RLock()
	defer s.mutex.RUnlock()
	return s.connected
}

// LastActivity returns the timestamp of the last successful delivery.
func (s *Subscriber[T]) LastActivity() time.Time {
	s.mutex.RLock()
	defer s.mutex.RUnlock()
	return s.lastActivity
}

// BusStatistics provides common statistics shared by stream buses.
type BusStatistics struct {
	TotalItems        uint64
	DroppedItems      uint64
	ActiveSubscribers int
	LastItemTime      time.Time
}

// Bus manages the distribution of streamed items to subscribers.
// It provides thread-safe publish-subscribe with filtering and statistics.
type Bus[T any] struct {
	name        string
	mutex       sync.RWMutex
	subscribers map[string]*Subscriber[T]
	statistics  BusStatistics
}

// NewBus constructs a generic stream bus with the given name for logging.
func NewBus[T any](name string) *Bus[T] {
	return &Bus[T]{
		name:        name,
		subscribers: make(map[string]*Subscriber[T]),
	}
}

// Subscribe registers a new subscriber with the bus.
func (b *Bus[T]) Subscribe(subscriber *Subscriber[T]) {
	b.mutex.Lock()
	defer b.mutex.Unlock()

	b.subscribers[subscriber.ID()] = subscriber
	b.statistics.ActiveSubscribers = len(b.subscribers)

	log.Infof("[%s] added subscriber: %s (total: %d)", b.name, subscriber.ID(), b.statistics.ActiveSubscribers)
}

// Unsubscribe removes a subscriber from the bus.
func (b *Bus[T]) Unsubscribe(subscriberID string) {
	b.mutex.Lock()
	defer b.mutex.Unlock()

	if subscriber, exists := b.subscribers[subscriberID]; exists {
		subscriber.Close()
		delete(b.subscribers, subscriberID)
		b.statistics.ActiveSubscribers = len(b.subscribers)

		log.Infof("[%s] removed subscriber: %s (total: %d)", b.name, subscriberID, b.statistics.ActiveSubscribers)
	}
}

// Publish delivers an item to all matching subscribers.
func (b *Bus[T]) Publish(meetingID string, item T) bool {
	b.mutex.RLock()
	subscribers := make([]*Subscriber[T], 0, len(b.subscribers))
	for _, sub := range b.subscribers {
		if sub.ShouldReceive(meetingID, item) {
			subscribers = append(subscribers, sub)
		}
	}
	b.mutex.RUnlock()

	b.mutex.Lock()
	b.statistics.TotalItems++
	b.statistics.LastItemTime = time.Now()
	b.mutex.Unlock()

	if len(subscribers) == 0 {
		return true
	}

	sent := 0
	for _, subscriber := range subscribers {
		if !subscriber.IsConnected() {
			continue
		}
		if subscriber.Send(item) {
			sent++
		} else {
			b.mutex.Lock()
			b.statistics.DroppedItems++
			b.mutex.Unlock()
		}
	}

	return sent > 0
}

// GetStatistics returns a snapshot of bus statistics.
func (b *Bus[T]) GetStatistics() BusStatistics {
	b.mutex.RLock()
	defer b.mutex.RUnlock()

	stats := b.statistics
	stats.ActiveSubscribers = len(b.subscribers)
	return stats
}

// GetSubscriberCount returns the number of active subscribers.
func (b *Bus[T]) GetSubscriberCount() int {
	b.mutex.RLock()
	defer b.mutex.RUnlock()
	return len(b.subscribers)
}

// CleanupInactiveSubscribers drops subscribers that have not received data within the timeout.
func (b *Bus[T]) CleanupInactiveSubscribers(timeout time.Duration) int {
	b.mutex.Lock()
	defer b.mutex.Unlock()

	now := time.Now()
	removed := 0

	for id, subscriber := range b.subscribers {
		if !subscriber.IsConnected() || now.Sub(subscriber.LastActivity()) > timeout {
			subscriber.Close()
			delete(b.subscribers, id)
			removed++
			log.Infof("[%s] cleaned up inactive subscriber: %s", b.name, id)
		}
	}

	if removed > 0 {
		b.statistics.ActiveSubscribers = len(b.subscribers)
		log.Infof("[%s] cleaned up %d inactive subscribers (total: %d)", b.name, removed, b.statistics.ActiveSubscribers)
	}

	return removed
}

// Shutdown closes all subscribers and resets the bus.
func (b *Bus[T]) Shutdown() {
	b.mutex.Lock()
	defer b.mutex.Unlock()

	log.Infof("[%s] shutting down bus", b.name)

	for _, subscriber := range b.subscribers {
		subscriber.Close()
	}

	b.subscribers = make(map[string]*Subscriber[T])
	b.statistics.ActiveSubscribers = 0

	log.Infof("[%s] bus shutdown complete", b.name)
}

// releasable represents items that can release underlying resources/buffers.
type releasable interface{ Release() }

// PublishOrRelease publishes the item and, if no subscriber receives it,
// calls Release() when the item implements it. Returns true if delivered.
func PublishOrRelease[T any](meetingID string, publisher interface{ Publish(string, T) bool }, item T) bool {
	if publisher.Publish(meetingID, item) {
		return true
	}
	if r, ok := any(item).(releasable); ok {
		r.Release()
	}
	return false
}
