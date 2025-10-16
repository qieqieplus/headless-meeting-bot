package server

import (
	"net/http"
	"sync"
	"time"

	"github.com/gorilla/websocket"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/audio"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/config"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/zoomsdk"
)

// WebSocketServer handles WebSocket connections for audio streaming
type WebSocketServer struct {
	upgrader       websocket.Upgrader
	audioBus       *audio.Bus
	meetingManager zoomsdk.MeetingManager
	config         *config.Config
	clients        map[string]*Client
	clientsMutex   sync.RWMutex
}

// NewWebSocketServer creates a new WebSocket server
func NewWebSocketServer(audioBus *audio.Bus, manager zoomsdk.MeetingManager, cfg *config.Config) *WebSocketServer {
	return &WebSocketServer{
		upgrader: websocket.Upgrader{
			CheckOrigin: func(r *http.Request) bool {
				return true
			},
		},
		audioBus:       audioBus,
		meetingManager: manager,
		config:         cfg,
		clients:        make(map[string]*Client),
	}
}

// HandleConnection handles incoming WebSocket connections
func (s *WebSocketServer) HandleConnection(w http.ResponseWriter, r *http.Request) {
	conn, err := s.upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Errorf("Failed to upgrade WebSocket connection: %v", err)
		return
	}

	meetingID := GetPathParam(r, "meeting_id")
	if meetingID == "" {
		log.Warnf("Missing meeting_id in path: %s", r.URL.Path)
		conn.Close()
		return
	}

	config := ParseConnectionConfig(r.URL.Query())
	config.MeetingID = meetingID

	client := NewClient(conn, s.audioBus, s.config)
	s.addClient(client)

	log.Infof("WebSocket client connected: %s for meeting: %s", client.ID, meetingID)

	client.Process(config)

	s.removeClient(client.ID)
	log.Infof("WebSocket client disconnected: %s", client.ID)
}

// addClient adds a client to the server's list
func (s *WebSocketServer) addClient(client *Client) {
	s.clientsMutex.Lock()
	defer s.clientsMutex.Unlock()
	s.clients[client.ID] = client
}

// removeClient removes a client from the server's list
func (s *WebSocketServer) removeClient(clientID string) {
	s.clientsMutex.Lock()
	defer s.clientsMutex.Unlock()
	delete(s.clients, clientID)
}

// Client represents a single WebSocket client
type Client struct {
	ID         string
	conn       *websocket.Conn
	audioBus   *audio.Bus
	config     *config.Config
	subscriber *audio.Subscriber
	sendChan   chan interface{} // Can be []byte (JSON) or *audio.AudioFrame
	stopChan   chan struct{}
}

// NewClient creates a new client
func NewClient(conn *websocket.Conn, audioBus *audio.Bus, cfg *config.Config) *Client {
	return &Client{
		ID:       conn.RemoteAddr().String(),
		conn:     conn,
		audioBus: audioBus,
		config:   cfg,
		sendChan: make(chan interface{}, 100),
		stopChan: make(chan struct{}),
	}
}

// Process starts processing messages for the client
func (c *Client) Process(config *ConnectionConfig) {
	c.subscriber = audio.NewSubscriber(c.ID, config.QueueSize)
	c.subscriber.SetMeetingFilter(config.MeetingID)
	c.subscriber.SetAudioTypeFilter(config.AudioTypes)
	c.subscriber.SetUserIDFilter(config.UserIDs)
	c.audioBus.Subscribe(c.subscriber)
	defer c.audioBus.Unsubscribe(c.ID)

	go c.writePump()
	go c.readPump()

	formatMsg, err := CreateAudioFormatMessage(
		c.config.AudioSampleRate,
		c.config.AudioChannels,
	)
	if err == nil {
		c.sendChan <- formatMsg
	}

	for frame := range c.subscriber.Channel {
		select {
		case c.sendChan <- frame:
		default:
			log.Warnf("Dropping frame for client %s (send channel full)", c.ID)
		}
	}
}

// writePump pumps messages from the send channel to the WebSocket connection
func (c *Client) writePump() {
	defer func() {
		c.conn.Close()
		close(c.stopChan)
	}()

	// Aggregate PCM per user and type, send every flush interval
	userBuffers := make(map[uint64]map[audio.AudioType][]byte)

	ticker := time.NewTicker(c.config.WebSocket.AudioFlushInterval)
	defer ticker.Stop()

	// Ping ticker to keep connection alive
	pingTicker := time.NewTicker(c.config.WebSocket.PingInterval)
	defer pingTicker.Stop()

	for {
		select {
		case message, ok := <-c.sendChan:
			if !ok {
				// Send channel closed
				c.conn.WriteMessage(websocket.CloseMessage, []byte{})
				return
			}

			switch msg := message.(type) {
			case []byte:
				// Direct message
				c.conn.SetWriteDeadline(time.Now().Add(c.config.WebSocket.WriteTimeout))
				if err := c.conn.WriteMessage(websocket.TextMessage, msg); err != nil {
					log.Errorf("Error writing text message to WebSocket: %v", err)
					return
				}
			case *audio.AudioFrame:
				// Accumulate binary audio frame for this user and type
				if userBuffers[msg.UserID] == nil {
					userBuffers[msg.UserID] = make(map[audio.AudioType][]byte)
				}
				userBuffers[msg.UserID][msg.Type] = append(userBuffers[msg.UserID][msg.Type], msg.Data...)
			}

		case <-ticker.C:
			// Flush all accumulated data per user and type
			for userID, typeMap := range userBuffers {
				for t, buf := range typeMap {
					if len(buf) == 0 {
						continue
					}
					frame := &audio.AudioFrame{
						UserID: userID,
						Type:   t,
						Data:   buf,
					}
					out := frame.Encode()
					c.conn.SetWriteDeadline(time.Now().Add(c.config.WebSocket.WriteTimeout))
					if err := c.conn.WriteMessage(websocket.BinaryMessage, out); err != nil {
						log.Errorf("Error writing audio to WebSocket: %v", err)
						return
					}
					// Clear buffer for next tick
					userBuffers[userID][t] = userBuffers[userID][t][:0]
				}
			}

		case <-pingTicker.C:
			// Send periodic ping to keep connection alive
			c.conn.SetWriteDeadline(time.Now().Add(c.config.WebSocket.WriteTimeout))
			if err := c.conn.WriteMessage(websocket.PingMessage, nil); err != nil {
				log.Errorf("Error sending ping to WebSocket: %v", err)
				return
			}
			log.Debugf("Sent ping to client %s", c.ID)

		case <-c.stopChan:
			return
		}
	}
}

// readPump pumps messages from the WebSocket connection
func (c *Client) readPump() {
	defer func() {
		c.subscriber.Close()
		c.conn.Close()
	}()

	// Set initial read deadline
	c.conn.SetReadDeadline(time.Now().Add(c.config.WebSocket.ReadTimeout))
	c.conn.SetPongHandler(func(string) error {
		// Reset read deadline when pong is received
		c.conn.SetReadDeadline(time.Now().Add(c.config.WebSocket.ReadTimeout))
		log.Debugf("Received pong from client %s", c.ID)
		return nil
	})

	for {
		_, _, err := c.conn.ReadMessage()
		if err != nil {
			if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
				log.Errorf("WebSocket read error: %v", err)
			}
			break
		}
		// If we receive any message (not just pong), reset the deadline
		c.conn.SetReadDeadline(time.Now().Add(c.config.WebSocket.ReadTimeout))
	}
}
