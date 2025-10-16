package zoomsdk

/*
#include <stdlib.h>
#include "zoom_sdk_c.h"

// Forward declaration for Go callback
extern void goOnAudioDataReceived(MeetingHandle meeting_handle, void* data, int length, int type, unsigned int node_id);

// C wrapper function that will be passed to zoom_meeting_set_audio_callback
static void cAudioCallback(MeetingHandle meeting_handle, const void* data, int length, int type, unsigned int node_id) {
    goOnAudioDataReceived(meeting_handle, (void*)data, length, type, node_id);
}

// Helper function to get the C callback function pointer
static OnAudioDataReceivedCallback getCCallbackPtr() {
    return cAudioCallback;
}
*/
import "C"
import (
	"fmt"
	"runtime"
	"sync"
	"unsafe"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/audio"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
)

// SDKHandle wraps the C SDK handle
type SDKHandle struct {
	handle C.ZoomSDKHandle
}

// MeetingHandle wraps the C meeting handle
type MeetingHandle struct {
	handle C.MeetingHandle
}

// Result represents the result of SDK operations
type Result int

const (
	ResultSuccess Result = 0
	ResultError   Result = -1
)

func (r Result) Error() string {
	switch r {
	case ResultSuccess:
		return "success"
	case ResultError:
		return "error"
	default:
		return fmt.Sprintf("unknown result code: %d", int(r))
	}
}

// Global registry to map C handles to Go objects
var (
	handleRegistry = make(map[uintptr]*MeetingInstance)
	registryMutex  sync.RWMutex
)

// registerMeetingHandle registers a meeting handle for callback routing
func registerMeetingHandle(handle C.MeetingHandle, instance *MeetingInstance) {
	registryMutex.Lock()
	defer registryMutex.Unlock()
	handleRegistry[uintptr(handle)] = instance
}

// unregisterMeetingHandle removes a meeting handle from the registry
func unregisterMeetingHandle(handle C.MeetingHandle) {
	registryMutex.Lock()
	defer registryMutex.Unlock()
	delete(handleRegistry, uintptr(handle))
}

// getMeetingInstance retrieves a meeting instance by handle
func getMeetingInstance(handle C.MeetingHandle) *MeetingInstance {
	registryMutex.RLock()
	defer registryMutex.RUnlock()
	return handleRegistry[uintptr(handle)]
}

// CreateSDK creates and initializes a new Zoom SDK instance
func CreateSDK(sdkKey, sdkSecret string) (*SDKHandle, error) {
	cKey := C.CString(sdkKey)
	defer C.free(unsafe.Pointer(cKey))

	cSecret := C.CString(sdkSecret)
	defer C.free(unsafe.Pointer(cSecret))

	handle := C.zoom_sdk_create(cKey, cSecret)
	if handle == nil {
		return nil, fmt.Errorf("failed to create SDK instance")
	}

	log.Debugf("Created SDK handle: %p", handle)
	return &SDKHandle{handle: handle}, nil
}

// Destroy cleans up and destroys the SDK instance
func (s *SDKHandle) Destroy() {
	if s.handle != nil {
		log.Debugf("Destroying SDK handle: %p", s.handle)
		C.zoom_sdk_destroy(s.handle)
		s.handle = nil
	}
}

// CreateAndJoinMeeting creates and joins a meeting
func (s *SDKHandle) CreateAndJoinMeeting(meetingID, password, displayName, joinToken string, enableAudio, enableVideo bool) (*MeetingHandle, error) {
	if s.handle == nil {
		return nil, fmt.Errorf("SDK handle is nil")
	}

	cMeetingID := C.CString(meetingID)
	defer C.free(unsafe.Pointer(cMeetingID))

	var cPassword *C.char
	if password != "" {
		cPassword = C.CString(password)
		defer C.free(unsafe.Pointer(cPassword))
	}

	var cDisplayName *C.char
	if displayName != "" {
		cDisplayName = C.CString(displayName)
		defer C.free(unsafe.Pointer(cDisplayName))
	}

	var cJoinToken *C.char
	if joinToken != "" {
		cJoinToken = C.CString(joinToken)
		defer C.free(unsafe.Pointer(cJoinToken))
	}
	// cJoinToken is nil when joinToken is empty, which is handled correctly by the C API

	var cEnableAudio, cEnableVideo C.int
	if enableAudio {
		cEnableAudio = 1
	}

	if enableVideo {
		cEnableVideo = 1
	}

	handle := C.zoom_meeting_create_and_join(s.handle, cMeetingID, cPassword, cDisplayName, cJoinToken, cEnableAudio, cEnableVideo)
	if handle == nil {
		return nil, fmt.Errorf("failed to create and join meeting")
	}

	log.Debugf("Created meeting handle: %p for meeting ID: %s", handle, meetingID)
	return &MeetingHandle{handle: handle}, nil
}

// Destroy leaves and destroys the meeting
func (m *MeetingHandle) Destroy() {
	if m.handle != nil {
		log.Debugf("Destroying meeting handle: %p", m.handle)
		unregisterMeetingHandle(m.handle)
		C.zoom_meeting_destroy(m.handle)
		m.handle = nil
	}
}

// SetAudioCallback sets the audio callback for the meeting
func (m *MeetingHandle) SetAudioCallback() error {
	if m.handle == nil {
		return fmt.Errorf("meeting handle is nil")
	}

	result := C.zoom_meeting_set_audio_callback(m.handle, C.getCCallbackPtr())
	if result != C.ZOOM_SDK_SUCCESS {
		return fmt.Errorf("failed to set audio callback: %s", Result(result).Error())
	}

	log.Debugf("Set audio callback for meeting handle: %p", m.handle)
	return nil
}

// GetStatus returns the current meeting status from Zoom SDK
func (m *MeetingHandle) GetStatus() MeetingStatus {
	if m.handle == nil {
		return MeetingStatus(8) // MEETING_STATUS_UNKNOWN
	}

	status := C.zoom_meeting_get_status(m.handle)
	return MeetingStatus(status)
}

// RunLoop runs the main SDK event loop (blocking)
func RunLoop() {
	log.Debug("Starting SDK event loop")
	C.zoom_sdk_run_loop()
	log.Debug("SDK event loop stopped")
}

// StopLoop requests the SDK event loop to stop
func StopLoop() {
	log.Debug("Requesting SDK event loop to stop")
	C.zoom_sdk_stop_loop()
}

//export goOnAudioDataReceived
func goOnAudioDataReceived(meetingHandle C.MeetingHandle, data unsafe.Pointer, length C.int, audioType C.int, nodeID C.uint) {
	instance := getMeetingInstance(meetingHandle)
	if instance == nil {
		log.Warnf("Received audio data for unknown meeting handle: %p", meetingHandle)
		return
	}

	audioData := C.GoBytes(data, length)

	frame := &audio.AudioFrame{
		Type:   audio.AudioType(audioType),
		UserID: uint64(nodeID),
		Data:   audioData,
	}

	select {
	case instance.audioChannel <- frame:
	default:
		log.Warnf("Dropping audio frame for meeting %s (channel full)", instance.meetingID)
	}
}

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

		log.Debug("OS thread started and locked")

		for {
			select {
			case cmd := <-t.commands:
				cmd()
			case <-t.done:
				log.Debug("OS thread stopping")
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
