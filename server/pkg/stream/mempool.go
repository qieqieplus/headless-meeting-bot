package stream

import "sync"

// BufferPool manages reusable byte slices for audio/video data
type BufferPool struct {
	pool sync.Pool
}

// NewBufferPool creates a new buffer pool
func NewBufferPool(initialSize int) *BufferPool {
	return &BufferPool{
		pool: sync.Pool{
			New: func() any {
				b := make([]byte, initialSize)
				return &b
			},
		},
	}
}

// Get retrieves a buffer from the pool
func (p *BufferPool) Get(minSize int) []byte {
	if minSize <= 0 {
		return nil
	}
	bufPtr := p.pool.Get().(*[]byte)
	if cap(*bufPtr) < minSize {
		*bufPtr = make([]byte, minSize)
	}
	buf := *bufPtr
	return buf[:minSize]
}

func (p *BufferPool) Put(buf []byte) {
	if buf == nil {
		return
	}
	buf = buf[:cap(buf)]
	p.pool.Put(&buf)
}

// Global buffer pool for audio frames
var (
	AudioBufferPool = NewBufferPool(4096)        // 4KB initial size for audio
	VideoBufferPool = NewBufferPool(1024 * 1024) // 1MB initial size for video segments
)
