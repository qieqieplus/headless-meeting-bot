package native

import (
	"runtime"
)

// OSThread represents a dedicated OS thread for running SDK operations
type OSThread struct {
	done     chan struct{}
	commands chan func()
}

// NewOSThread creates a new OS thread
func NewOSThread() *OSThread {
	return &OSThread{
		done:     make(chan struct{}),
		commands: make(chan func(), 10),
	}
}

// Start starts the OS thread and locks it
func (t *OSThread) Start() {
	go func() {
		runtime.LockOSThread()
		defer runtime.UnlockOSThread()

		for {
			select {
			case cmd := <-t.commands:
				cmd()
			case <-t.done:
				return
			}
		}
	}()
}

// Execute runs a function on the OS thread
func (t *OSThread) Execute(fn func()) {
	done := make(chan struct{})
	t.commands <- func() {
		fn()
		close(done)
	}
	<-done
}

// Stop stops the OS thread
func (t *OSThread) Stop() {
	close(t.done)
}
