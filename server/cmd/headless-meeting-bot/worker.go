package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/audio"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/zoomsdk"
)

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
	config   *WorkerConfig
	instance *zoomsdk.MeetingInstance
	audioBus *audio.Bus
	server   *http.Server
}

func startWorker(configJSON string) {
	// Parse configuration
	var config WorkerConfig
	if err := json.Unmarshal([]byte(configJSON), &config); err != nil {
		log.Fatalf("Failed to parse config: %v", err)
	}

	// Initialize logger
	log.Init("info")
	log.Infof("Starting meeting worker for meeting: %s on port %d (PID: %d)", config.MeetingID, config.WorkerPort, os.Getpid())

	// Create and start worker
	worker := NewWorker(&config)
	if err := worker.Start(); err != nil {
		log.Fatalf("Failed to start worker: %v", err)
	}

	// Wait for shutdown signal
	worker.WaitForShutdown()
}

// NewWorker creates a new meeting worker
func NewWorker(config *WorkerConfig) *Worker {
	return &Worker{
		config:   config,
		audioBus: audio.NewBus(),
	}
}

// Start starts the worker and joins the meeting
func (w *Worker) Start() error {
	// Create meeting instance
	meetingConfig := &zoomsdk.MeetingConfig{
		MeetingID:   w.config.MeetingID,
		Password:    w.config.Password,
		DisplayName: w.config.DisplayName,
		JoinToken:   w.config.JoinToken,
		EnableAudio: w.config.EnableAudio,
		EnableVideo: w.config.EnableVideo,
		SDKKey:      w.config.SDKKey,
		SDKSecret:   w.config.SDKSecret,
	}

	w.instance = zoomsdk.NewMeetingInstance(meetingConfig, w.audioBus)

	// Start HTTP server for status and audio streaming
	mux := http.NewServeMux()
	mux.HandleFunc("/health", w.handleHealth)
	mux.HandleFunc("/status", w.handleStatus)
	mux.HandleFunc("/stats", w.handleStats)
	mux.HandleFunc("/audio", w.handleAudioStream)

	w.server = &http.Server{
		Addr:    fmt.Sprintf(":%d", w.config.WorkerPort),
		Handler: mux,
	}

	// Start HTTP server in background
	go func() {
		log.Infof("Worker HTTP server listening on :%d", w.config.WorkerPort)
		if err := w.server.ListenAndServe(); err != http.ErrServerClosed {
			log.Errorf("Worker HTTP server error: %v", err)
		}
	}()

	// Give server a moment to start
	time.Sleep(100 * time.Millisecond)

	// Start meeting
	log.Infof("Joining meeting: %s", w.config.MeetingID)
	if err := w.instance.Start(); err != nil {
		return fmt.Errorf("failed to start meeting: %w", err)
	}

	// Send ready notification to callback URL
	go w.notifyReady()

	return nil
}

// handleHealth returns health status
func (w *Worker) handleHealth(rw http.ResponseWriter, r *http.Request) {
	rw.Header().Set("Content-Type", "application/json")
	json.NewEncoder(rw).Encode(map[string]interface{}{
		"status": "ok",
		"pid":    os.Getpid(),
	})
}

// handleStatus returns meeting status
func (w *Worker) handleStatus(rw http.ResponseWriter, r *http.Request) {
	status := w.instance.GetStatus()
	rw.Header().Set("Content-Type", "application/json")
	json.NewEncoder(rw).Encode(map[string]interface{}{
		"meeting_id": w.config.MeetingID,
		"status":     status.String(),
	})
}

// handleStats returns meeting statistics
func (w *Worker) handleStats(rw http.ResponseWriter, r *http.Request) {
	stats := w.instance.GetStats()
	rw.Header().Set("Content-Type", "application/json")
	json.NewEncoder(rw).Encode(stats)
}

// handleAudioStream streams audio frames to the client
func (w *Worker) handleAudioStream(rw http.ResponseWriter, r *http.Request) {
	// Set headers for streaming
	rw.Header().Set("Content-Type", "application/octet-stream")
	rw.Header().Set("Transfer-Encoding", "chunked")

	flusher, ok := rw.(http.Flusher)
	if !ok {
		http.Error(rw, "Streaming not supported", http.StatusInternalServerError)
		return
	}

	// Subscribe to audio bus
	subscriberID := fmt.Sprintf("worker-%s-%s", w.config.MeetingID, r.RemoteAddr)
	subscriber := audio.NewSubscriber(subscriberID, 1000)
	subscriber.SetMeetingFilter(w.config.MeetingID)
	w.audioBus.Subscribe(subscriber)
	defer w.audioBus.Unsubscribe(subscriberID)

	log.Infof("Audio stream started for meeting: %s", w.config.MeetingID)

	// Stream frames
	for frame := range subscriber.Channel {
		// Encode frame as JSON with newline delimiter
		frameData, err := json.Marshal(frame)
		if err != nil {
			log.Errorf("Failed to marshal audio frame: %v", err)
			continue
		}

		// Write frame with newline delimiter
		if _, err := fmt.Fprintf(rw, "%s\n", frameData); err != nil {
			log.Errorf("Failed to write audio frame: %v", err)
			break
		}

		flusher.Flush()
	}

	log.Infof("Audio stream ended for meeting: %s", w.config.MeetingID)
}

// notifyReady sends a notification to the callback URL when ready
func (w *Worker) notifyReady() {
	if w.config.CallbackURL == "" {
		return
	}

	// Wait a bit
	time.Sleep(1 * time.Second)

	// Send ready notification
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

// WaitForShutdown waits for a shutdown signal
func (w *Worker) WaitForShutdown() {
	// Create channel to listen for OS signals
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGINT, syscall.SIGTERM)

	// Block until a signal is received
	<-stop

	log.Info("Shutting down worker...")

	// Stop meeting
	if w.instance != nil {
		if err := w.instance.Stop(); err != nil {
			log.Errorf("Error stopping meeting: %v", err)
		}
	}

	// Shutdown HTTP server
	if w.server != nil {
		if err := w.server.Close(); err != nil {
			log.Errorf("Error shutting down HTTP server: %v", err)
		}
	}

	log.Info("Worker shutdown complete")
}
