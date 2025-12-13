package stream

import "time"

// FileType represents the type of HLS/video file streamed over the bus.
type FileType int

const (
	FileTypePlaylist FileType = 0 // m3u8 playlist
	FileTypeInit     FileType = 1 // init.mp4
	FileTypeSegment  FileType = 2 // segment.m4s
)

// FileEvent represents a streamed video/HLS artifact.
type FileEvent struct {
	MeetingID  string
	Filename   string
	Data       []byte
	IsPlaylist bool
	Sequence   uint64
	Timestamp  time.Time
}

// NewVideoPlaylistEvent creates a new playlist FileEvent for HLS.
func NewVideoPlaylistEvent(meetingID, filename, content string) *FileEvent {
	return &FileEvent{
		MeetingID:  meetingID,
		Filename:   filename,
		Data:       []byte(content),
		IsPlaylist: true,
		Timestamp:  time.Now(),
	}
}

// NewVideoFileEvent creates a new binary FileEvent (init or segment), copying from C memory into a pooled buffer.
func NewVideoFileEvent(meetingID, filename string, cData []byte, sequence uint64) *FileEvent {
	// Get pooled buffer and copy C memory into it
	buf := VideoBufferPool.Get(len(cData))
	copy(buf, cData)
	return &FileEvent{
		MeetingID:  meetingID,
		Filename:   filename,
		Data:       buf,
		IsPlaylist: false,
		Sequence:   sequence,
		Timestamp:  time.Now(),
	}
}

// Release returns the buffer to the pool for binary segments.
func (e *FileEvent) Release() {
	if e != nil && !e.IsPlaylist && e.Data != nil {
		VideoBufferPool.Put(e.Data)
		e.Data = nil
	}
}

// VideoSubscriber represents a client subscribed to video streams.
type VideoSubscriber = Subscriber[*FileEvent]

// NewVideoSubscriber creates a new video subscriber.
func NewVideoSubscriber(id string, bufferSize int) *VideoSubscriber {
	return NewSubscriber[*FileEvent](id, bufferSize)
}

// VideoBus manages video file distribution to subscribers.
type VideoBus = Bus[*FileEvent]

// NewVideoBus creates a new video bus.
func NewVideoBus() *VideoBus {
	return NewBus[*FileEvent]("video")
}
