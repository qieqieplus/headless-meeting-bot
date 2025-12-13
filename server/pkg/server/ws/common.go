package ws

import (
	"encoding/json"
	"fmt"
	"net/http"
	"strconv"
	"sync"
	"time"

	"github.com/gorilla/websocket"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/config"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/stream"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/zoombot"
)

type WSConnInfo struct {
	ID        string
	Type      string // "audio", "events", "video"
	MeetingID string
	StartedAt int64
}

type WebSocketServer struct {
	upgrader       websocket.Upgrader
	audioBus       *stream.AudioBus
	eventsBus      *stream.EventBus
	videoBus       *stream.VideoBus
	meetingManager zoombot.MeetingManager
	config         *config.Config
	clients        map[string]WSConnInfo
	clientsMutex   sync.RWMutex
	activeConns    map[string]*activeConn
	activeConnsMu  sync.RWMutex
}

func NewWebSocketServer(audioBus *stream.AudioBus, eventsBus *stream.EventBus, videoBus *stream.VideoBus, manager zoombot.MeetingManager, cfg *config.Config) *WebSocketServer {
	server := &WebSocketServer{
		upgrader: websocket.Upgrader{
			CheckOrigin: func(r *http.Request) bool {
				return true
			},
		},
		audioBus:       audioBus,
		eventsBus:      eventsBus,
		videoBus:       videoBus,
		meetingManager: manager,
		config:         cfg,
		clients:        make(map[string]WSConnInfo),
		activeConns:    make(map[string]*activeConn),
	}

	if eventsBus != nil {
		server.startMeetingMonitor()
	}

	return server
}

// upgradeAndMeetingID upgrades the connection and extracts meeting_id from path
func (s *WebSocketServer) upgradeAndMeetingID(w http.ResponseWriter, r *http.Request) (*websocket.Conn, string, error) {
	conn, err := s.upgrader.Upgrade(w, r, nil)
	if err != nil {
		return nil, "", fmt.Errorf("failed to upgrade WebSocket connection: %w", err)
	}

	meetingID := GetPathParam(r, "meeting_id")
	if meetingID == "" {
		conn.Close()
		return nil, "", fmt.Errorf("missing meeting_id in path: %s", r.URL.Path)
	}

	return conn, meetingID, nil
}

func (s *WebSocketServer) registerConn(connID, connType, meetingID string) {
	s.clientsMutex.Lock()
	defer s.clientsMutex.Unlock()
	s.clients[connID] = WSConnInfo{
		ID:        connID,
		Type:      connType,
		MeetingID: meetingID,
		StartedAt: time.Now().Unix(),
	}
}

func (s *WebSocketServer) unregisterConn(connID string) {
	s.clientsMutex.Lock()
	defer s.clientsMutex.Unlock()
	delete(s.clients, connID)
}

type activeConn struct {
	conn *websocket.Conn
	stop func()
	once sync.Once
}

func (ac *activeConn) Stop() {
	if ac.stop != nil {
		ac.once.Do(ac.stop)
	}
}

func (s *WebSocketServer) registerActiveConn(connID string, conn *websocket.Conn, stop func()) {
	s.activeConnsMu.Lock()
	s.activeConns[connID] = &activeConn{
		conn: conn,
		stop: stop,
	}
	s.activeConnsMu.Unlock()
}

func (s *WebSocketServer) unregisterActiveConn(connID string) {
	s.activeConnsMu.Lock()
	delete(s.activeConns, connID)
	s.activeConnsMu.Unlock()
}

// GetConnectionsByMeeting returns a snapshot of connections for the provided meeting.
func (s *WebSocketServer) GetConnectionsByMeeting(meetingID string) []WSConnInfo {
	s.clientsMutex.RLock()
	defer s.clientsMutex.RUnlock()

	var infos []WSConnInfo
	for _, info := range s.clients {
		if info.MeetingID == meetingID {
			infos = append(infos, info)
		}
	}
	return infos
}

// CloseConnectionsForMeeting sends a close control frame and stops all connections for a meeting.
func (s *WebSocketServer) CloseConnectionsForMeeting(meetingID string) {
	conns := s.GetConnectionsByMeeting(meetingID)
	if len(conns) == 0 {
		return
	}

	log.Infof("Closing %d WebSocket connections for meeting %s", len(conns), meetingID)

	for _, info := range conns {
		s.activeConnsMu.RLock()
		active := s.activeConns[info.ID]
		s.activeConnsMu.RUnlock()
		if active == nil || active.conn == nil {
			continue
		}

		deadline := time.Now().Add(2 * time.Second)
		if err := active.conn.WriteControl(
			websocket.CloseMessage,
			websocket.FormatCloseMessage(websocket.CloseNormalClosure, "meeting ended"),
			deadline,
		); err != nil {
			if !websocket.IsUnexpectedCloseError(err) {
				// Normal closure, no logging needed
			} else {
				log.Warnf("Error sending close frame to %s: %v", info.ID, err)
			}
		}

		active.Stop()
	}
}

func (s *WebSocketServer) startMeetingMonitor() {
	subscriberID := "ws-monitor"
	subscriber := stream.NewEventSubscriber(subscriberID, 128)
	s.eventsBus.Subscribe(subscriber)

	go func() {
		defer s.eventsBus.Unsubscribe(subscriberID)
		for event := range subscriber.Channel() {
			if event == nil {
				continue
			}
			if event.Type == stream.EventTypeMeetingStatus && event.Status == "ended" {
				log.Infof("WebSocket server observed meeting %s ending", event.MeetingID)
				s.CloseConnectionsForMeeting(event.MeetingID)
			}
		}
	}()
}

// startPingLoop returns a stop function that should be called to clean up
func startPingLoop(conn *websocket.Conn, cfg *config.Config, connID string) func() {
	conn.SetReadDeadline(time.Now().Add(cfg.WebSocket.ReadTimeout))
	conn.SetPongHandler(func(string) error {
		conn.SetReadDeadline(time.Now().Add(cfg.WebSocket.ReadTimeout))
		return nil
	})

	stopChan := make(chan struct{})
	pingTicker := time.NewTicker(cfg.WebSocket.PingInterval)

	go func() {
		defer pingTicker.Stop()
		for {
			select {
			case <-pingTicker.C:
				conn.SetWriteDeadline(time.Now().Add(cfg.WebSocket.WriteTimeout))
				if err := conn.WriteMessage(websocket.PingMessage, nil); err != nil {
					log.Errorf("Error sending ping to WebSocket %s: %v", connID, err)
					return
				}
			case <-stopChan:
				return
			}
		}
	}()

	return func() {
		close(stopChan)
	}
}

// GetPathParam fetches a single path parameter populated by the ParamRouter
func GetPathParam(r *http.Request, name string) string {
	params, _ := r.Context().Value("path_params").(map[string]string)
	if params == nil {
		return ""
	}
	return params[name]
}

const (
	MessageTypeAudioFormat = "audio_format"
)

// AudioFormatMessage is sent as the first message to inform clients about audio format
type AudioFormatMessage struct {
	Type         string `json:"type"`
	SampleRate   int    `json:"sample_rate"`
	Channels     int    `json:"channels"`
	SampleFormat string `json:"sample_format"`
}

func CreateAudioFormatMessage(sampleRate, channels int, encoding string) ([]byte, error) {
	msg := AudioFormatMessage{
		Type:         MessageTypeAudioFormat,
		SampleRate:   sampleRate,
		Channels:     channels,
		SampleFormat: encoding,
	}

	return json.Marshal(msg)
}

type ConnectionConfig struct {
	MeetingID  string
	AudioTypes []stream.AudioType
	UserIDs    []uint64
	QueueSize  int
}

func ParseConnectionConfig(params map[string][]string) *ConnectionConfig {
	config := &ConnectionConfig{
		QueueSize: 1000, // Default queue size
	}

	if meetingIDs, ok := params["meeting_id"]; ok && len(meetingIDs) > 0 {
		config.MeetingID = meetingIDs[0]
	}

	if types, ok := params["type"]; ok {
		for _, typeStr := range types {
			switch typeStr {
			case "mixed":
				config.AudioTypes = append(config.AudioTypes, stream.AudioTypeMixed)
			case "one_way":
				config.AudioTypes = append(config.AudioTypes, stream.AudioTypeOneWay)
			case "share":
				config.AudioTypes = append(config.AudioTypes, stream.AudioTypeShare)
			}
		}
	}

	if userIDs, ok := params["user_id"]; ok {
		for _, userIDStr := range userIDs {
			var userID uint64
			if _, err := fmt.Sscanf(userIDStr, "%d", &userID); err == nil {
				config.UserIDs = append(config.UserIDs, userID)
			}
		}
	}

	if sizes, ok := params["queue_size"]; ok && len(sizes) > 0 {
		if value, err := strconv.Atoi(sizes[0]); err == nil && value > 0 {
			config.QueueSize = value
		}
	}

	return config
}
