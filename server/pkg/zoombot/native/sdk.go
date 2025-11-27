//go:build cgo

package native

/*
#include <stdlib.h>
#include "zoom_bot_c.h"
*/
import "C"

import (
	"fmt"
	"unsafe"
)

// CreateSDK creates and initializes a new Zoom SDK instance
func CreateSDK(sdkKey, sdkSecret string) (*SDKHandle, error) {
	cKey := C.CString(sdkKey)
	defer C.free(unsafe.Pointer(cKey))

	cSecret := C.CString(sdkSecret)
	defer C.free(unsafe.Pointer(cSecret))

	handle := C.zoom_bot_create(cKey, cSecret)
	if handle == nil {
		return nil, fmt.Errorf("failed to create SDK instance")
	}

	return &SDKHandle{handle: handle}, nil
}

// Destroy cleans up and destroys the SDK instance
func (s *SDKHandle) Destroy() {
	if s.handle != nil {
		C.zoom_bot_destroy(s.handle)
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

	var cEnableAudio, cEnableVideo C.int
	if enableAudio {
		cEnableAudio = 1
	}
	if enableVideo {
		cEnableVideo = 1
	}

	handle := C.zoom_bot_meeting_create_and_join(s.handle, cMeetingID, cPassword, cDisplayName, cJoinToken, cEnableAudio, cEnableVideo)
	if handle == nil {
		return nil, fmt.Errorf("failed to create and join meeting")
	}

	return &MeetingHandle{handle: handle}, nil
}

// RunLoop runs the main SDK event loop (blocking)
func RunLoop() {
	C.zoom_bot_run_loop()
}

// StopLoop requests the SDK event loop to stop
func StopLoop() {
	C.zoom_bot_stop_loop()
}



