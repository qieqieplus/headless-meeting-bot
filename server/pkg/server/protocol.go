package server

import (
	"encoding/json"
	"fmt"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/audio"
)

// WebSocket message types
const (
	MessageTypeAudioFormat = "audio_format"
	MessageTypeError       = "error"
	MessageTypeHeartbeat   = "heartbeat"
)

// AudioFormatMessage is sent as the first message to inform clients about audio format
type AudioFormatMessage struct {
	Type         string `json:"type"`
	SampleRate   int    `json:"sample_rate"`
	Channels     int    `json:"channels"`
	SampleFormat string `json:"sample_format"`
}

// ErrorMessage is sent when an error occurs
type ErrorMessage struct {
	Type  string `json:"type"`
	Error string `json:"error"`
	Code  int    `json:"code,omitempty"`
}

// HeartbeatMessage is sent periodically to keep connection alive
type HeartbeatMessage struct {
	Type      string `json:"type"`
	Timestamp int64  `json:"timestamp"`
}

// CreateAudioFormatMessage creates the initial audio format message
func CreateAudioFormatMessage(sampleRate, channels int) ([]byte, error) {
	msg := AudioFormatMessage{
		Type:         MessageTypeAudioFormat,
		SampleRate:   sampleRate,
		Channels:     channels,
		SampleFormat: "s16le",
	}

	return json.Marshal(msg)
}

// CreateErrorMessage creates an error message
func CreateErrorMessage(errMsg string, code int) ([]byte, error) {
	msg := ErrorMessage{
		Type:  MessageTypeError,
		Error: errMsg,
		Code:  code,
	}

	return json.Marshal(msg)
}

// CreateHeartbeatMessage creates a heartbeat message
func CreateHeartbeatMessage(timestamp int64) ([]byte, error) {
	msg := HeartbeatMessage{
		Type:      MessageTypeHeartbeat,
		Timestamp: timestamp,
	}

	return json.Marshal(msg)
}

// Deprecated: per-frame encoding moved to audio.AudioFrame.Encode()

// ConnectionConfig holds configuration for WebSocket connections
type ConnectionConfig struct {
	MeetingID         string
	AudioTypes        []audio.AudioType
	UserIDs           []uint64
	QueueSize         int
	EnableHeartbeat   bool
	HeartbeatInterval int // seconds
}

// ParseConnectionConfig parses connection configuration from query parameters
func ParseConnectionConfig(params map[string][]string) *ConnectionConfig {
	config := &ConnectionConfig{
		QueueSize:         1000, // Default queue size
		EnableHeartbeat:   true,
		HeartbeatInterval: 30, // 30 seconds
	}

	// Parse meeting ID
	if meetingIDs, ok := params["meeting_id"]; ok && len(meetingIDs) > 0 {
		config.MeetingID = meetingIDs[0]
	}

	// Parse audio types
	if types, ok := params["type"]; ok {
		for _, typeStr := range types {
			switch typeStr {
			case "mixed":
				config.AudioTypes = append(config.AudioTypes, audio.AudioTypeMixed)
			case "one_way":
				config.AudioTypes = append(config.AudioTypes, audio.AudioTypeOneWay)
			case "share":
				config.AudioTypes = append(config.AudioTypes, audio.AudioTypeShare)
			}
		}
	}

	// Parse node IDs
	if userIDs, ok := params["user_id"]; ok {
		for _, userIDStr := range userIDs {
			var userID uint64
			if _, err := fmt.Sscanf(userIDStr, "%d", &userID); err == nil {
				config.UserIDs = append(config.UserIDs, userID)
			}
		}
	}

	return config
}
