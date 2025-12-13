package main

import (
	"bytes"
	"context"
	"encoding/gob"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/server"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/stream"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/zoombot"
)

const (
	streamBufferSize       = 1000
	videoStreamBufferSize  = 100
	serverStartDelay       = 100 * time.Millisecond
	readyNotificationDelay = 1 * time.Second
	shutdownTimeout        = 3 * time.Second
)

func init() {
	// Register types with GOB for efficient encoding/decoding
	gob.Register(&stream.AudioEvent{})
	gob.Register(&stream.Event{})
	gob.Register(&stream.FileEvent{})
	gob.Register(&zoombot.MeetingState{})
}

// WorkerConfig holds the configuration passed from the main server
type WorkerConfig struct {
	MeetingID   string `json:"meeting_id"`
	Password    string `json:"password"`
	DisplayName string `json:"display_name"`
	JoinToken   string `json:"join_token"`
	EnableAudio bool   `json:"enable_audio"`
	EnableVideo bool   `json:"enable_video"`
	SDKKey      string `json:"sdk_key"`
	SDKSecret   string `json:"sdk_secret"`
	WorkerPort  int    `json:"worker_port"`
	CallbackURL string `json:"callback_url"`
}

// Worker manages a single meeting instance
type Worker struct {
	config       *WorkerConfig
	instance     *zoombot.MeetingInstance
	audioBus     *stream.AudioBus
	eventsBus    *stream.EventBus
	videoBus     *stream.VideoBus
	server       *http.Server
	shutdownOnce sync.Once
	stopCh       chan struct{}
}

func startWorker(configJSON string) {
	var config WorkerConfig
	if err := json.Unmarshal([]byte(configJSON), &config); err != nil {
		log.Fatalf("Failed to parse config: %v", err)
	}

	log.Init("info")
	log.Infof("Starting meeting worker for meeting: %s on port %d (PID: %d)", config.MeetingID, config.WorkerPort, os.Getpid())

	worker := NewWorker(&config)
	if err := worker.Start(); err != nil {
		log.Fatalf("Failed to start worker: %v", err)
	}

	worker.WaitForShutdown()
}

// NewWorker creates a new meeting worker
func NewWorker(config *WorkerConfig) *Worker {
	return &Worker{
		config:    config,
		audioBus:  stream.NewAudioBus(),
		eventsBus: stream.NewEventBus(),
		videoBus:  stream.NewVideoBus(),
		stopCh:    make(chan struct{}),
	}
}

// Start starts the worker and joins the meeting
func (w *Worker) Start() error {
	// Create meeting instance
	meetingConfig := &zoombot.MeetingConfig{
		MeetingID:   w.config.MeetingID,
		Password:    w.config.Password,
		DisplayName: w.config.DisplayName,
		JoinToken:   w.config.JoinToken,
		EnableAudio: w.config.EnableAudio,
		EnableVideo: w.config.EnableVideo,
		SDKKey:      w.config.SDKKey,
		SDKSecret:   w.config.SDKSecret,
	}

	w.instance = zoombot.NewMeetingInstance(meetingConfig, w.audioBus, w.eventsBus, w.videoBus)

	// Start HTTP server for state, audio, events, and video streaming
	w.server = &http.Server{
		Addr:    fmt.Sprintf(":%d", w.config.WorkerPort),
		Handler: w.setupRoutes(),
	}

	go func() {
		log.Infof("Worker HTTP server listening on :%d", w.config.WorkerPort)
		if err := w.server.ListenAndServe(); err != http.ErrServerClosed {
			log.Errorf("Worker HTTP server error: %v", err)
		}
	}()

	// Give server a moment to start
	time.Sleep(serverStartDelay)

	log.Infof("Joining meeting: %s", w.config.MeetingID)
	if err := w.instance.Start(); err != nil {
		return fmt.Errorf("failed to start meeting: %w", err)
	}

	go w.notifyReady()

	return nil
}

// setupRoutes configures HTTP routes for the worker
func (w *Worker) setupRoutes() *http.ServeMux {
	mux := http.NewServeMux()
	mux.HandleFunc("/health", w.handleHealth)
	mux.HandleFunc("/state", w.handleState)
	mux.HandleFunc("/audio", w.handleAudioStream)
	mux.HandleFunc("/events", w.handleEventsStream)
	mux.HandleFunc("/video", w.handleVideoStream)
	mux.HandleFunc("/shutdown", w.handleShutdown)
	return mux
}

// shutdownBuses closes all buses to unblock stream handlers
func (w *Worker) shutdownBuses() {
	if w.audioBus != nil {
		w.audioBus.Shutdown()
	}
	if w.eventsBus != nil {
		w.eventsBus.Shutdown()
	}
	if w.videoBus != nil {
		w.videoBus.Shutdown()
	}
}

// streamGOB is a generic helper for streaming GOB-encoded data from a subscriber
func streamGOB[T any](
	rw http.ResponseWriter,
	r *http.Request,
	streamType string,
	meetingID string,
	subscriberID string,
	subscribe func(),
	unsubscribe func(),
	channel <-chan T,
	cleanup func(T),
) {
	// Set headers for streaming
	contentType := "application/x-gob"
	if streamType == "audio" {
		contentType = "application/octet-stream"
	}
	rw.Header().Set("Content-Type", contentType)

	flusher, ok := rw.(http.Flusher)
	if !ok {
		server.WriteJSONError(rw, "Streaming not supported", http.StatusInternalServerError)
		return
	}

	subscribe()
	defer unsubscribe()

	enc := gob.NewEncoder(rw)
	log.Infof("%s stream started for meeting: %s", streamType, meetingID)

	for {
		select {
		case <-r.Context().Done():
			log.Infof("%s stream cancelled for meeting: %s", streamType, meetingID)
			return
		case item, ok := <-channel:
			if !ok {
				log.Infof("%s stream ended for meeting: %s", streamType, meetingID)
				return
			}

			if err := enc.Encode(item); err != nil {
				log.Errorf("Failed to encode %s item: %v", streamType, err)
				if cleanup != nil {
					cleanup(item)
				}
				return
			}

			flusher.Flush()
			if cleanup != nil {
				cleanup(item)
			}
		}
	}
}

func (w *Worker) handleHealth(rw http.ResponseWriter, r *http.Request) {
	rw.Header().Set("Content-Type", "application/json")
	rw.WriteHeader(http.StatusOK)
	json.NewEncoder(rw).Encode(map[string]interface{}{
		"status": "ok",
		"pid":    os.Getpid(),
	})
}

// handleState returns meeting state based on mask query parameter
func (w *Worker) handleState(rw http.ResponseWriter, r *http.Request) {
	// Parse mask from query parameter, default to StateAll if not provided
	mask := zoombot.StateAll
	if maskStr := r.URL.Query().Get("mask"); maskStr != "" {
		var maskVal uint32
		if _, err := fmt.Sscanf(maskStr, "%d", &maskVal); err == nil {
			mask = zoombot.StateMask(maskVal)
		}
	}

	state := zoombot.MeetingState{
		MeetingID: w.config.MeetingID,
	}

	if mask&zoombot.StateStatus != 0 {
		state.Status = w.instance.GetStatusInfo()
	}

	if mask&zoombot.StateStatistics != 0 {
		state.Statistics = w.instance.GetStatistics()
	}

	if mask&zoombot.StateUsers != 0 {
		state.Users = w.instance.GetUsers()
	}

	rw.Header().Set("Content-Type", "application/x-gob")
	rw.WriteHeader(http.StatusOK)
	if err := gob.NewEncoder(rw).Encode(state); err != nil {
		log.Errorf("Failed to encode state: %v", err)
	}
}

func (w *Worker) handleShutdown(rw http.ResponseWriter, r *http.Request) {
	rw.Header().Set("Content-Type", "application/json")
	rw.WriteHeader(http.StatusOK)
	_ = json.NewEncoder(rw).Encode(map[string]string{"status": "shutting-down"})
	go w.Shutdown()
}

func (w *Worker) handleAudioStream(rw http.ResponseWriter, r *http.Request) {
	subscriberID := fmt.Sprintf("worker-%s-%s", w.config.MeetingID, r.RemoteAddr)
	subscriber := stream.NewAudioSubscriber(subscriberID, streamBufferSize)
	subscriber.SetMeetingFilter(w.config.MeetingID)

	streamGOB(rw, r, "audio", w.config.MeetingID, subscriberID,
		func() { w.audioBus.Subscribe(subscriber) },
		func() { w.audioBus.Unsubscribe(subscriberID) },
		subscriber.Channel(),
		func(item *stream.AudioEvent) {
			item.Release()
		},
	)
}

// handleEventsStream streams events (meeting status and user events) to the client
func (w *Worker) handleEventsStream(rw http.ResponseWriter, r *http.Request) {
	subscriberID := fmt.Sprintf("worker-events-%s-%s", w.config.MeetingID, r.RemoteAddr)
	subscriber := stream.NewEventSubscriber(subscriberID, streamBufferSize)
	subscriber.SetMeetingFilter(w.config.MeetingID)

	streamGOB(rw, r, "events", w.config.MeetingID, subscriberID,
		func() { w.eventsBus.Subscribe(subscriber) },
		func() { w.eventsBus.Unsubscribe(subscriberID) },
		subscriber.Channel(),
		nil,
	)
}

func (w *Worker) handleVideoStream(rw http.ResponseWriter, r *http.Request) {
	subscriberID := fmt.Sprintf("worker-video-%s-%s", w.config.MeetingID, r.RemoteAddr)
	subscriber := stream.NewVideoSubscriber(subscriberID, videoStreamBufferSize)
	subscriber.SetMeetingFilter(w.config.MeetingID)

	streamGOB(rw, r, "video", w.config.MeetingID, subscriberID,
		func() { w.videoBus.Subscribe(subscriber) },
		func() { w.videoBus.Unsubscribe(subscriberID) },
		subscriber.Channel(),
		func(item *stream.FileEvent) {
			if item.IsPlaylist {
				log.Infof("[video] worker sending playlist: meeting=%s file=%s len=%d", w.config.MeetingID, item.Filename, len(item.Data))
			}
			item.Release()
		},
	)
}

// notifyReady sends a notification to the callback URL when ready
func (w *Worker) notifyReady() {
	if w.config.CallbackURL == "" {
		return
	}

	time.Sleep(readyNotificationDelay)

	data := map[string]interface{}{
		"meeting_id":  w.config.MeetingID,
		"status":      "ready",
		"worker_port": w.config.WorkerPort,
		"pid":         os.Getpid(),
	}

	jsonData, _ := json.Marshal(data)
	resp, err := http.Post(w.config.CallbackURL, "application/json", bytes.NewReader(jsonData))
	if err != nil {
		log.Errorf("Failed to send ready notification: %v", err)
		return
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		log.Errorf("Ready notification failed with status: %d", resp.StatusCode)
	} else {
		log.Infof("Ready notification sent successfully")
	}
}

func (w *Worker) WaitForShutdown() {
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGINT, syscall.SIGTERM)

	select {
	case <-stop:
		w.Shutdown()
	case <-w.stopCh:
		// shutdown completed via HTTP handler
	}
}

func (w *Worker) Shutdown() {
	w.shutdownOnce.Do(func() {
		log.Info("Shutting down worker...")

		if w.instance != nil {
			if err := w.instance.Stop(); err != nil {
				log.Errorf("Error stopping meeting: %v", err)
			}
		}

		// Close all buses to unblock stream handlers immediately
		w.shutdownBuses()

		if w.server != nil {
			ctx, cancel := context.WithTimeout(context.Background(), shutdownTimeout)
			defer cancel()
			if err := w.server.Shutdown(ctx); err != nil {
				log.Errorf("Error shutting down HTTP server: %v", err)
			}
		}

		log.Info("Worker shutdown complete")
		close(w.stopCh)
	})
}
