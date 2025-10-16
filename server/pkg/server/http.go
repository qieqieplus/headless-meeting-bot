package server

import (
	"encoding/json"
	"net/http"
	"strings"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/zoomsdk"
)

// HTTPServer handles REST API requests
type HTTPServer struct {
	meetingManager zoomsdk.MeetingManager
	wsServer       *WebSocketServer
	router         http.Handler
}

// NewHTTPServer creates a new HTTP server
func NewHTTPServer(manager zoomsdk.MeetingManager, wsServer *WebSocketServer) *HTTPServer {
	server := &HTTPServer{
		meetingManager: manager,
		wsServer:       wsServer,
		router:         http.NewServeMux(),
	}
	server.registerRoutes()
	return server
}

// ServeHTTP implements the http.Handler interface
func (s *HTTPServer) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	log.Infof("Received request: %s %s", r.Method, r.URL.Path)
	s.router.ServeHTTP(w, r)
}

// registerRoutes sets up the API routes
func (s *HTTPServer) registerRoutes() {
	mux := http.NewServeMux()
	mux.HandleFunc("/health", s.handleHealth)
	mux.HandleFunc("/api/meetings", s.handleMeetings)
	mux.HandleFunc("/api/meetings/", s.handleMeetingByID)

	// Param router for websocket path: /ws/audio/{meeting_id}
	pr := NewParamRouter()
	pr.Handle("/ws/audio/{meeting_id}", s.wsServer.HandleConnection)

	// Delegate: if path starts with /ws/, use param router; else use mux
	s.router = http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if strings.HasPrefix(r.URL.Path, "/ws/") {
			pr.ServeHTTP(w, r)
			return
		}
		mux.ServeHTTP(w, r)
	})
}

// handleMeetings handles requests for the /api/meetings endpoint
func (s *HTTPServer) handleMeetings(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodPost:
		s.handleJoinMeeting(w, r)
	case http.MethodGet:
		s.handleListMeetings(w, r)
	default:
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
	}
}

// handleMeetingByID handles requests for /api/meetings/{id}
func (s *HTTPServer) handleMeetingByID(w http.ResponseWriter, r *http.Request) {
	meetingID := strings.TrimPrefix(r.URL.Path, "/api/meetings/")

	switch r.Method {
	case http.MethodDelete:
		s.handleLeaveMeeting(w, r, meetingID)
	case http.MethodGet:
		s.handleGetMeetingStatus(w, r, meetingID)
	default:
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
	}
}

// JoinMeetingRequest is the request body for joining a meeting
type JoinMeetingRequest struct {
	MeetingID   string `json:"meeting_id"`
	Password    string `json:"password"`
	DisplayName string `json:"display_name"`
	JoinToken   string `json:"join_token,omitempty"`
}

// handleJoinMeeting handles joining a new meeting
func (s *HTTPServer) handleJoinMeeting(w http.ResponseWriter, r *http.Request) {
	var req JoinMeetingRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "Invalid request body", http.StatusBadRequest)
		return
	}

	if req.MeetingID == "" {
		http.Error(w, "Meeting ID is required", http.StatusBadRequest)
		return
	}

	err := s.meetingManager.JoinMeeting(req.MeetingID, req.Password, req.DisplayName, req.JoinToken, true, false)
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusAccepted)
	json.NewEncoder(w).Encode(map[string]string{"status": "joining"})
}

// handleLeaveMeeting handles leaving a meeting
func (s *HTTPServer) handleLeaveMeeting(w http.ResponseWriter, r *http.Request, meetingID string) {
	err := s.meetingManager.LeaveMeeting(meetingID)
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	json.NewEncoder(w).Encode(map[string]string{"status": "left"})
}

// handleListMeetings handles listing all meetings
func (s *HTTPServer) handleListMeetings(w http.ResponseWriter, r *http.Request) {
	meetings := s.meetingManager.ListMeetings()

	type meetingStatus struct {
		MeetingID string `json:"meeting_id"`
		Status    string `json:"status"`
	}

	response := make([]meetingStatus, 0, len(meetings))
	for id, status := range meetings {
		response = append(response, meetingStatus{MeetingID: id, Status: status.String()})
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

// handleGetMeetingStatus handles getting a single meeting's status
func (s *HTTPServer) handleGetMeetingStatus(w http.ResponseWriter, r *http.Request, meetingID string) {
	// Get status from ListMeetings
	meetings := s.meetingManager.ListMeetings()
	status, exists := meetings[meetingID]
	if !exists {
		http.Error(w, "Meeting not found", http.StatusNotFound)
		return
	}

	stats, _ := s.meetingManager.GetMeetingStats(meetingID)

	response := map[string]interface{}{
		"meeting_id": meetingID,
		"status":     status.String(),
		"error":      nil,
		"stats":      stats,
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

// handleHealth returns health status for the process manager
func (s *HTTPServer) handleHealth(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"status":        "ok",
		"meeting_count": s.meetingManager.GetMeetingCount(),
	})
}
