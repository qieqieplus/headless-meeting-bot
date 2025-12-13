package zoombot

import "errors"

var (
	// ErrMeetingAlreadyExists is returned when attempting to join a meeting that is already active
	ErrMeetingAlreadyExists = errors.New("meeting already exists")
	// ErrMeetingNotFound is returned when attempting to access a meeting that doesn't exist
	ErrMeetingNotFound = errors.New("meeting not found")
)
