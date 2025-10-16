package audio

import (
	"sync"
)

// BufferPool manages reusable byte slices for audio data
type BufferPool struct {
	pool sync.Pool
}

// NewBufferPool creates a new buffer pool
func NewBufferPool(initialSize int) *BufferPool {
	return &BufferPool{
		pool: sync.Pool{
			New: func() interface{} {
				return make([]byte, initialSize)
			},
		},
	}
}

// Get retrieves a buffer from the pool
func (p *BufferPool) Get(minSize int) []byte {
	buf := p.pool.Get().([]byte)
	if cap(buf) < minSize {
		return make([]byte, minSize)
	}
	return buf[:minSize]
}

// Put returns a buffer to the pool
func (p *BufferPool) Put(buf []byte) {
	// Reset length to capacity for reuse
	buf = buf[:cap(buf)]
	p.pool.Put(buf)
}

// Global buffer pool for audio frames
var globalBufferPool = NewBufferPool(4096) // 4KB initial size

// GetBuffer gets a buffer from the global pool
func GetBuffer(size int) []byte {
	return globalBufferPool.Get(size)
}

// PutBuffer returns a buffer to the global pool
func PutBuffer(buf []byte) {
	globalBufferPool.Put(buf)
}
