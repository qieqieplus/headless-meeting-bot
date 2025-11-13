package ws

import (
	"net/http"
	"sync"
	"time"

	"github.com/gorilla/websocket"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/config"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/stream"
)

func (s *WebSocketServer) HandleAudioConnection(w http.ResponseWriter, r *http.Request) {
	conn, meetingID, err := s.upgradeAndMeetingID(w, r)
	if err != nil {
		log.Errorf("Audio connection setup failed: %v", err)
		return
	}

	config := ParseConnectionConfig(r.URL.Query())
	config.MeetingID = meetingID

	client := NewAudioClient(conn, s.audioBus, s.config)
	connID := client.ID
	s.registerConn(connID, "audio", meetingID)
	defer s.unregisterConn(connID)

	s.registerActiveConn(connID, conn, client.Stop)
	defer s.unregisterActiveConn(connID)

	log.Infof("WebSocket client connected: %s (audio) for meeting: %s", connID, meetingID)

	client.Process(config)

	log.Infof("WebSocket client disconnected: %s (audio)", connID)
}

type AudioClient struct {
	ID         string
	conn       *websocket.Conn
	audioBus   *stream.AudioBus
	config     *config.Config
	subscriber *stream.AudioSubscriber
	sendChan   chan interface{} // Can be []byte (JSON) or *stream.AudioEvent
	stopChan   chan struct{}
	stopOnce   sync.Once
}

func NewAudioClient(conn *websocket.Conn, audioBus *stream.AudioBus, cfg *config.Config) *AudioClient {
	return &AudioClient{
		ID:       conn.RemoteAddr().String(),
		conn:     conn,
		audioBus: audioBus,
		config:   cfg,
		sendChan: make(chan interface{}, 100),
		stopChan: make(chan struct{}),
	}
}

func (c *AudioClient) Process(config *ConnectionConfig) {
	c.subscriber = stream.NewAudioSubscriber(c.ID, config.QueueSize)
	c.subscriber.SetMeetingFilter(config.MeetingID)
	c.subscriber.SetAudioTypeFilter(config.AudioTypes)
	c.subscriber.SetUserIDFilter(config.UserIDs)
	c.audioBus.Subscribe(c.subscriber)
	defer c.audioBus.Unsubscribe(c.ID)

	defer close(c.sendChan)
	defer c.Stop()

	go c.writePump()
	go c.readPump()

	formatMsg, err := CreateAudioFormatMessage(
		c.config.AudioSampleRate,
		c.config.AudioChannels,
	)
	if err == nil {
		c.sendChan <- formatMsg
	}

	framesCh := c.subscriber.Channel()

	for {
		select {
		case <-c.stopChan:
			return
		case frame, ok := <-framesCh:
			if !ok {
				return
			}
			select {
			case c.sendChan <- frame:
			default:
				log.Warnf("Dropping frame for client %s (send channel full)", c.ID)
			}
		}
	}
}

// writePump pumps messages from the send channel to the WebSocket connection
func (c *AudioClient) writePump() {
	defer func() {
		c.conn.Close()
		c.Stop()
	}()

	userBuffers := make(map[uint64]map[stream.AudioType][]byte)

	ticker := time.NewTicker(c.config.WebSocket.AudioFlushInterval)
	defer ticker.Stop()

	pingTicker := time.NewTicker(c.config.WebSocket.PingInterval)
	defer pingTicker.Stop()

	for {
		select {
		case message, ok := <-c.sendChan:
			if !ok {
				c.conn.WriteMessage(websocket.CloseMessage, []byte{})
				return
			}

			switch msg := message.(type) {
			case []byte:
				c.conn.SetWriteDeadline(time.Now().Add(c.config.WebSocket.WriteTimeout))
				if err := c.conn.WriteMessage(websocket.TextMessage, msg); err != nil {
					log.Errorf("Error writing text message to WebSocket: %v", err)
					return
				}
			case *stream.AudioEvent:
				// Accumulate binary audio frame for this user and type
				if userBuffers[msg.UserID] == nil {
					userBuffers[msg.UserID] = make(map[stream.AudioType][]byte)
				}
				userBuffers[msg.UserID][msg.Type] = append(userBuffers[msg.UserID][msg.Type], msg.Data...)
				msg.Release()
			}

		case <-ticker.C:
			// Flush all accumulated data per user and type
			for userID, typeMap := range userBuffers {
				for t, buf := range typeMap {
					if len(buf) == 0 {
						continue
					}
					frame := &stream.AudioEvent{
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
					userBuffers[userID][t] = userBuffers[userID][t][:0]
				}
			}

		case <-pingTicker.C:
			c.conn.SetWriteDeadline(time.Now().Add(c.config.WebSocket.WriteTimeout))
			if err := c.conn.WriteMessage(websocket.PingMessage, nil); err != nil {
				log.Errorf("Error sending ping to WebSocket: %v", err)
				return
			}

		case <-c.stopChan:
			return
		}
	}
}

func (c *AudioClient) readPump() {
	defer func() {
		c.subscriber.Close()
		c.conn.Close()
	}()

	c.conn.SetReadDeadline(time.Now().Add(c.config.WebSocket.ReadTimeout))
	c.conn.SetPongHandler(func(string) error {
		c.conn.SetReadDeadline(time.Now().Add(c.config.WebSocket.ReadTimeout))
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

func (c *AudioClient) Stop() {
	c.stopOnce.Do(func() {
		close(c.stopChan)
	})
}
