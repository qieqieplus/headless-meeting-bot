package server

import (
	"encoding/json"
	"errors"
	"net/http"
	"strings"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/manifest"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/server/ws"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/zoombot"
)

type HTTPServer struct {
	meetingManager zoombot.MeetingManager
	wsServer       *ws.WebSocketServer
	router         http.Handler
	manifest       manifest.Tracker
}

// writeJSONError has been moved to pkg/server/errors.go

func NewHTTPServer(manager zoombot.MeetingManager, wsServer *ws.WebSocketServer, tracker manifest.Tracker) *HTTPServer {
	server := &HTTPServer{
		meetingManager: manager,
		wsServer:       wsServer,
		manifest:       tracker,
		router:         http.NewServeMux(),
	}
	server.registerRoutes()
	return server
}

func (s *HTTPServer) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	log.Infof("Received request: %s %s", r.Method, r.URL.Path)
	s.router.ServeHTTP(w, r)
}

func (s *HTTPServer) registerRoutes() {
	mux := http.NewServeMux()
	mux.HandleFunc("/health", s.handleHealth)
	mux.HandleFunc("/api/meetings", s.handleMeetings)
	mux.HandleFunc("/api/meetings/", s.handleMeetingByID)

	pr := NewParamRouter()
	pr.Handle("/ws/audio/{meeting_id}", s.wsServer.HandleAudioConnection)
	pr.Handle("/ws/events/{meeting_id}", s.wsServer.HandleEventsConnection)
	pr.Handle("/ws/video/{meeting_id}", s.wsServer.HandleVideoConnection)
	pr.Handle("/api/meetings/{meeting_id}/manifest", http.HandlerFunc(s.handleGetManifest))

	// Delegate: if path starts with /ws/ or manifest route, use param router; else use mux
	s.router = http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if strings.HasPrefix(r.URL.Path, "/ws/") || strings.Contains(r.URL.Path, "/manifest") {
			pr.ServeHTTP(w, r)
			return
		}
		mux.ServeHTTP(w, r)
	})
}

func (s *HTTPServer) handleMeetings(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodPost:
		s.handleJoinMeeting(w, r)
	case http.MethodGet:
		s.handleListMeetings(w, r)
	default:
		w.Header().Set("Allow", "GET, POST")
		WriteJSONError(w, "Method not allowed", http.StatusMethodNotAllowed)
	}
}

func (s *HTTPServer) handleMeetingByID(w http.ResponseWriter, r *http.Request) {
	meetingID := strings.TrimPrefix(r.URL.Path, "/api/meetings/")

	switch r.Method {
	case http.MethodDelete:
		s.handleLeaveMeeting(w, r, meetingID)
	case http.MethodGet:
		s.handleGetMeetingState(w, r, meetingID)
	default:
		w.Header().Set("Allow", "GET, DELETE")
		WriteJSONError(w, "Method not allowed", http.StatusMethodNotAllowed)
	}
}

type JoinMeetingRequest struct {
	MeetingID   string `json:"meeting_id"`
	Password    string `json:"password"`
	DisplayName string `json:"display_name"`
	JoinToken   string `json:"join_token,omitempty"`
	EnableAudio bool   `json:"enable_audio"`
	EnableVideo bool   `json:"enable_video"`
}

func (s *HTTPServer) handleJoinMeeting(w http.ResponseWriter, r *http.Request) {
	var req JoinMeetingRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		WriteJSONError(w, "Invalid request body", http.StatusBadRequest)
		return
	}

	if req.MeetingID == "" {
		WriteJSONError(w, "Meeting ID is required", http.StatusBadRequest)
		return
	}

	// Default to audio recording
	if !req.EnableAudio && !req.EnableVideo {
		log.Warnf("JoinMeeting for %s without enable flags; defaulting enable_audio=true", req.MeetingID)
		req.EnableAudio = true
	}

	err := s.meetingManager.JoinMeeting(req.MeetingID, req.Password, req.DisplayName, req.JoinToken, req.EnableAudio, req.EnableVideo)
	if err != nil {
		if errors.Is(err, zoombot.ErrMeetingAlreadyExists) {
			WriteJSONError(w, err.Error(), http.StatusBadRequest)
		} else {
			WriteJSONError(w, err.Error(), http.StatusInternalServerError)
		}
		return
	}

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusAccepted)
	json.NewEncoder(w).Encode(map[string]string{"status": "joining"})
}

func (s *HTTPServer) handleLeaveMeeting(w http.ResponseWriter, r *http.Request, meetingID string) {
	err := s.meetingManager.LeaveMeeting(meetingID)
	if err != nil {
		if errors.Is(err, zoombot.ErrMeetingNotFound) {
			WriteJSONError(w, err.Error(), http.StatusNotFound)
		} else {
			WriteJSONError(w, err.Error(), http.StatusInternalServerError)
		}
		return
	}

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	json.NewEncoder(w).Encode(map[string]string{"status": "left"})
}

func (s *HTTPServer) handleListMeetings(w http.ResponseWriter, r *http.Request) {
	meetings := s.meetingManager.ListMeetings()

	type meetingStatus struct {
		MeetingID string             `json:"meeting_id"`
		Status    zoombot.StatusInfo `json:"status"`
	}

	response := make([]meetingStatus, 0, len(meetings))
	for id, status := range meetings {
		response = append(response, meetingStatus{MeetingID: id, Status: status})
	}

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	json.NewEncoder(w).Encode(response)
}

func (s *HTTPServer) handleGetMeetingState(w http.ResponseWriter, r *http.Request, meetingID string) {
	state, err := s.meetingManager.State(meetingID, zoombot.StateAll)
	if err != nil {
		if errors.Is(err, zoombot.ErrMeetingNotFound) {
			WriteJSONError(w, err.Error(), http.StatusNotFound)
		} else {
			WriteJSONError(w, err.Error(), http.StatusInternalServerError)
		}
		return
	}

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	json.NewEncoder(w).Encode(state)
}

func (s *HTTPServer) handleHealth(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	json.NewEncoder(w).Encode(map[string]interface{}{
		"status":        "ok",
		"meeting_count": s.meetingManager.GetMeetingCount(),
	})
}

func (s *HTTPServer) handleGetManifest(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		w.Header().Set("Allow", "GET")
		WriteJSONError(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	// Extract meeting_id from path using ParamRouter context
	paramsVal := r.Context().Value("path_params")
	if paramsVal == nil {
		WriteJSONError(w, "Missing meeting_id parameter", http.StatusBadRequest)
		return
	}

	params, ok := paramsVal.(map[string]string)
	if !ok {
		WriteJSONError(w, "Invalid path parameters", http.StatusBadRequest)
		return
	}

	meetingIDStr := params["meeting_id"]
	if meetingIDStr == "" {
		WriteJSONError(w, "Invalid meeting_id parameter", http.StatusBadRequest)
		return
	}

	manifestData, err := s.manifest.GetManifest(meetingIDStr)
	if err != nil {
		if errors.Is(err, manifest.ErrMeetingNotFound) {
			WriteJSONError(w, "Meeting manifest not found", http.StatusNotFound)
		} else {
			log.Errorf("Failed to get manifest for meeting %s: %v", meetingIDStr, err)
			WriteJSONError(w, "Internal server error", http.StatusInternalServerError)
		}
		return
	}

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	if err := json.NewEncoder(w).Encode(manifestData); err != nil {
		log.Errorf("Failed to encode manifest for meeting %s: %v", meetingIDStr, err)
	}
}
