package ws

import (
	"encoding/json"
	"fmt"
	"net/http"
	"time"

	"github.com/gorilla/websocket"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/stream"
)

func (s *WebSocketServer) HandleEventsConnection(w http.ResponseWriter, r *http.Request) {
	conn, meetingID, err := s.upgradeAndMeetingID(w, r)
	if err != nil {
		log.Errorf("Events connection setup failed: %v", err)
		return
	}

	subscriberID := conn.RemoteAddr().String() + "-events"
	s.registerConn(subscriberID, "events", meetingID)
	defer s.unregisterConn(subscriberID)

	subscriber := stream.NewEventSubscriber(subscriberID, 1000)
	subscriber.SetMeetingFilter(meetingID)
	s.eventsBus.Subscribe(subscriber)
	defer s.eventsBus.Unsubscribe(subscriberID)

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

	log.Infof("Events WebSocket client connected: %s (events) for meeting: %s", subscriberID, meetingID)

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
					log.Errorf("Events WebSocket read error for %s: %v", subscriberID, err)
				}
				break
			}
			conn.SetReadDeadline(time.Now().Add(s.config.WebSocket.ReadTimeout))
		}
	}()

	if err := s.sendUserSnapshot(conn, meetingID); err != nil {
		log.Warnf("Failed to send user snapshot for %s: %v", meetingID, err)
	}

	eventsCh := subscriber.Channel()

	defer func() {
		conn.Close()
		<-readDone
		log.Infof("Events WebSocket client disconnected: %s (events)", subscriberID)
	}()

	// Stream events
	for {
		select {
		case <-stopChan:
			log.Infof("Events WebSocket server closed connection: %s (meeting: %s)", subscriberID, meetingID)
			return
		case event, ok := <-eventsCh:
			if !ok {
				return
			}
			if err := s.writeEvent(conn, event); err != nil {
				log.Errorf("Events WebSocket write error for %s (meeting: %s): %v", subscriberID, meetingID, err)
				return
			}
		}
	}
}

func (s *WebSocketServer) sendUserSnapshot(conn *websocket.Conn, meetingID string) error {
	if s.meetingManager == nil {
		return nil
	}

	/*
		If the bot has not joined the meeting,
		the users list will be empty slice,
		and the snapshot event will be triggered when the bot joins the meeting.
		So we don't need to send the snapshot event here.
	*/
	users, err := s.meetingManager.Users(meetingID)
	if err != nil {
		return fmt.Errorf("failed to fetch meeting users: %w", err)
	}

	now := time.Now().UnixMilli()
	for _, user := range users {
		event := stream.NewUserEvent(meetingID, stream.UserEventSnapshot, user, now)
		if err := s.writeEvent(conn, event); err != nil {
			return fmt.Errorf("failed to write snapshot event: %w", err)
		}
	}

	return nil
}

func (s *WebSocketServer) writeEvent(conn *websocket.Conn, event *stream.Event) error {
	eventData, err := json.Marshal(event)
	if err != nil {
		return fmt.Errorf("failed to marshal event: %w", err)
	}

	conn.SetWriteDeadline(time.Now().Add(s.config.WebSocket.WriteTimeout))
	if err := conn.WriteMessage(websocket.TextMessage, eventData); err != nil {
		return fmt.Errorf("failed to write event to WebSocket: %w", err)
	}

	return nil
}
