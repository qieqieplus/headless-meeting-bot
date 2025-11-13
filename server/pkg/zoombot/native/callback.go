//go:build cgo

package native

/*
#include <stdlib.h>
#include "zoom_bot_c.h"
*/
import "C"

import (
	"sync"
	"unsafe"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
)

// EventSink is implemented by upper layers to receive events for a meeting
type EventSink interface {
	OnAudio(data []byte, audioType int, nodeID uint64)
	OnMeetingStatusChanged(status MeetingStatus, detail int)
	OnUserStatusEvent(event *UserStatusEvent)
	OnHlsFile(filename string, data []byte, isPlaylist bool, sequence uint64)
}

// Global registry to map C handles to Go event sinks
var (
	handleRegistry = make(map[uintptr]EventSink)
	registryMutex  sync.RWMutex
)

func registerMeetingHandle(handle C.MeetingHandle, sink EventSink) {
	registryMutex.Lock()
	handleRegistry[uintptr(handle)] = sink
	registryMutex.Unlock()
}

func unregisterMeetingHandle(handle C.MeetingHandle) {
	registryMutex.Lock()
	delete(handleRegistry, uintptr(handle))
	registryMutex.Unlock()
}

func getEventSink(handle C.MeetingHandle) EventSink {
	registryMutex.RLock()
	sink := handleRegistry[uintptr(handle)]
	registryMutex.RUnlock()
	return sink
}

//export OnAudioDataReceived
func OnAudioDataReceived(meetingHandle C.MeetingHandle, data unsafe.Pointer, length C.int, audioType C.int, nodeID C.uint) {
	sink := getEventSink(meetingHandle)
	if sink == nil {
		log.Warnf("Received audio data for unknown meeting handle: %p", meetingHandle)
		return
	}
	sink.OnAudio(ToSliceFromBytes[byte](data, uintptr(length)), int(audioType), uint64(nodeID))
}

//export OnMeetingStatusChanged
func OnMeetingStatusChanged(meetingHandle C.MeetingHandle, status C.int, detail C.int) {
	sink := getEventSink(meetingHandle)
	if sink == nil {
		log.Warnf("Received status update for unknown meeting handle: %p", meetingHandle)
		return
	}
	sink.OnMeetingStatusChanged(MeetingStatus(status), int(detail))
}

//export OnUserStatusEvent
func OnUserStatusEvent(meetingHandle C.MeetingHandle, event *C.ZoomUserStatusEvent) {
	sink := getEventSink(meetingHandle)
	if sink == nil {
		log.Warnf("Received user status event for unknown meeting handle: %p", meetingHandle)
		return
	}
	if event == nil {
		log.Warn("Received nil user status event")
		return
	}
	goEvent := UserStatusEvent{
		EventType: UserEventType(event.event),
		UserID:    uint64(event.user.id),
		Name:      C.GoString(event.user.name),
		Audio:     int(event.user.audio),
		Video:     int(event.user.video),
		Share:     int(event.user.share),
		Timestamp: uint64(event.timestamp_ms),
	}
	sink.OnUserStatusEvent(&goEvent)
}

//export OnHlsFile
func OnHlsFile(meetingHandle C.MeetingHandle, filename *C.char, filedata *C.uchar, size C.size_t, isPlaylist C.int, sequence C.uint64_t) {
	sink := getEventSink(meetingHandle)
	if sink == nil {
		log.Warnf("Received HLS file for unknown meeting handle: %p", meetingHandle)
		return
	}
	name := C.GoString(filename)
	sink.OnHlsFile(name, ToSliceFromBytes[byte](unsafe.Pointer(filedata), uintptr(size)), int(isPlaylist) != 0, uint64(sequence))
}
