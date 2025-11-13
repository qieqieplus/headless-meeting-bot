package ws

import (
	"encoding/json"
	"net/http"
	"time"

	"github.com/gorilla/websocket"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/stream"
)

const BinaryHeaderSize = 64

func (s *WebSocketServer) HandleVideoConnection(w http.ResponseWriter, r *http.Request) {
	conn, meetingID, err := s.upgradeAndMeetingID(w, r)
	if err != nil {
		log.Errorf("Video connection setup failed: %v", err)
		return
	}

	subscriberID := conn.RemoteAddr().String() + "-video"
	s.registerConn(subscriberID, "video", meetingID)
	defer s.unregisterConn(subscriberID)

	subscriber := stream.NewVideoSubscriber(subscriberID, 100)
	subscriber.SetMeetingFilter(meetingID)
	s.videoBus.Subscribe(subscriber)
	defer s.videoBus.Unsubscribe(subscriberID)

	stopChan := make(chan struct{})
	s.registerActiveConn(subscriberID, conn, func() {
		select {
		case <-stopChan:
			return
		default:
			close(stopChan)
		}
	})
	defer s.unregisterActiveConn(subscriberID)

	log.Infof("Video WebSocket client connected: %s (video) for meeting: %s", subscriberID, meetingID)

	stopPing := startPingLoop(conn, s.config, subscriberID)
	defer stopPing()

	// Start read loop to handle pong messages and reset deadline
	readDone := make(chan struct{})
	go func() {
		defer close(readDone)
		for {
			_, _, err := conn.ReadMessage()
			if err != nil {
				if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
					log.Errorf("Video WebSocket read error for %s: %v", subscriberID, err)
				}
				break
			}
			conn.SetReadDeadline(time.Now().Add(s.config.WebSocket.ReadTimeout))
		}
	}()

	defer func() {
		conn.Close()
		<-readDone
		log.Infof("Video WebSocket client disconnected: %s (video)", subscriberID)
	}()

	videoCh := subscriber.Channel()

	for {
		select {
		case <-stopChan:
			log.Infof("Video WebSocket server closed connection: %s (meeting: %s)", subscriberID, meetingID)
			return
		case fileEvent, ok := <-videoCh:
			if !ok {
				return
			}

			if fileEvent.IsPlaylist {
				// For playlists, send as text JSON with content
				payload := map[string]interface{}{
					"filename":    fileEvent.Filename,
					"is_playlist": true,
					"sequence":    fileEvent.Sequence,
					"content":     string(fileEvent.Data),
				}

				msgData, err := json.Marshal(payload)
				if err != nil {
					log.Errorf("Failed to marshal playlist: %v", err)
					continue
				}

				conn.SetWriteDeadline(time.Now().Add(s.config.WebSocket.WriteTimeout))
				if err := conn.WriteMessage(websocket.TextMessage, msgData); err != nil {
					log.Errorf("Error writing playlist to WebSocket: %v", err)
					return
				}
			} else {
				// For segments, send filename as fixed-length binary header (64 bytes) before video data
				// Format: [64 bytes: filename bytes (UTF-8, NUL-padded)][video data]
				filenameBytes := []byte(fileEvent.Filename)
				if len(filenameBytes) > BinaryHeaderSize {
					log.Errorf("Filename too long for segment (max %d): %s", BinaryHeaderSize, fileEvent.Filename)
					fileEvent.Release()
					continue
				}

				totalLen := BinaryHeaderSize + len(fileEvent.Data)
				buf := make([]byte, totalLen)

				// Write fixed-length filename header (NUL-padded)
				copy(buf[:BinaryHeaderSize], filenameBytes)

				// Write video data
				copy(buf[BinaryHeaderSize:], fileEvent.Data)

				// Send as single binary message
				conn.SetWriteDeadline(time.Now().Add(s.config.WebSocket.WriteTimeout))
				if err := conn.WriteMessage(websocket.BinaryMessage, buf); err != nil {
					log.Errorf("Error writing segment to WebSocket: %v", err)
					fileEvent.Release()
					return
				}

				// Return buffer to pool
				fileEvent.Release()
			}
		}
	}
}
