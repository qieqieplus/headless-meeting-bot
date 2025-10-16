package main

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/audio"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
	. "github.com/qieqieplus/headless-meeting-bot/server/pkg/zoomsdk"
)

// ProcessManager manages meeting worker processes
type ProcessManager struct {
	workers     sync.Map // map[string]*WorkerProcess
	audioBus    *audio.Bus
	sdkKey      string
	sdkSecret   string
	usedPorts   sync.Map // map[int]bool
	nextPort    uint32
	basePort    int
	maxPort     int
	workerBin   string
	callbackURL string
}

// WorkerProcess represents a running meeting worker process
type WorkerProcess struct {
	MeetingID string
	Port      int
	PID       int
	Status    MeetingStatus
	Stats     MeetingStats
	statsMux  sync.RWMutex
	cmd       *exec.Cmd
	cancel    context.CancelFunc
	stopChan  chan struct{}
	stopped   bool
	config    *MeetingConfig
}

func NewProcessManager(sdkKey, sdkSecret string, audioBus *audio.Bus) (*ProcessManager, error) {
	workerBin, err := os.Executable()
	if err != nil {
		return nil, fmt.Errorf("failed to get executable path: %w", err)
	}

	// Resolve symlinks if any
	workerBin, err = filepath.EvalSymlinks(workerBin)
	if err != nil {
		return nil, fmt.Errorf("failed to resolve executable path: %w", err)
	}

	log.Infof("Using worker binary: %s worker", workerBin)

	return &ProcessManager{
		audioBus:  audioBus,
		sdkKey:    sdkKey,
		sdkSecret: sdkSecret,
		basePort:  9000,
		maxPort:   9999, // Allow 1000 concurrent workers
		workerBin: workerBin,
	}, nil
}

func (pm *ProcessManager) JoinMeeting(meetingID, password, displayName, joinToken string, enableAudio, enableVideo bool) error {
	if _, exists := pm.workers.Load(meetingID); exists {
		return fmt.Errorf("meeting %s already exists", meetingID)
	}

	config := &MeetingConfig{
		MeetingID:   meetingID,
		Password:    password,
		DisplayName: displayName,
		JoinToken:   joinToken,
		EnableAudio: enableAudio,
		EnableVideo: enableVideo,
		SDKKey:      pm.sdkKey,
		SDKSecret:   pm.sdkSecret,
	}

	port := pm.allocatePort()
	if port == 0 {
		return fmt.Errorf("no available ports")
	}

	worker, err := pm.spawnWorker(config, port)
	if err != nil {
		pm.usedPorts.Delete(port)
		return fmt.Errorf("failed to spawn worker: %w", err)
	}

	if _, loaded := pm.workers.LoadOrStore(meetingID, worker); loaded {
		worker.Stop()
		pm.usedPorts.Delete(port)
		return fmt.Errorf("meeting %s already exists", meetingID)
	}

	// Start audio streaming from worker
	go pm.streamAudioFromWorker(worker)

	log.Infof("Successfully spawned worker for meeting: %s (PID: %d, Port: %d)", meetingID, worker.PID, worker.Port)
	return nil
}

func (pm *ProcessManager) allocatePort() int {
	for i := 0; i < pm.maxPort-pm.basePort; i++ {
		candidate := pm.basePort + int(atomic.AddUint32(&pm.nextPort, 1))%(pm.maxPort-pm.basePort)
		if _, loaded := pm.usedPorts.LoadOrStore(candidate, true); !loaded {
			return candidate
		}
	}
	return 0
}

func (pm *ProcessManager) spawnWorker(config *MeetingConfig, port int) (*WorkerProcess, error) {
	workerConfig := map[string]interface{}{
		"meeting_id":   config.MeetingID,
		"password":     config.Password,
		"display_name": config.DisplayName,
		"join_token":   config.JoinToken,
		"enable_audio": config.EnableAudio,
		"enable_video": config.EnableVideo,
		"sdk_key":      config.SDKKey,
		"sdk_secret":   config.SDKSecret,
		"worker_port":  port,
		"callback_url": pm.callbackURL,
	}

	configJSON, err := json.Marshal(workerConfig)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal worker config: %w", err)
	}

	ctx, cancel := context.WithCancel(context.Background())

	// Create command - use the same binary with "worker" subcommand
	cmd := exec.CommandContext(ctx, pm.workerBin, "worker", "-config", string(configJSON))
	cmd.Stdout = os.Stdout // Forward stdout for debugging
	cmd.Stderr = os.Stderr // Forward stderr for debugging

	// Ensure worker is killed if parent dies unexpectedly
	cmd.SysProcAttr = &syscall.SysProcAttr{
		Pdeathsig: syscall.SIGKILL,
	}

	// Start the process
	if err := cmd.Start(); err != nil {
		cancel()
		return nil, fmt.Errorf("failed to start worker process: %w", err)
	}

	worker := &WorkerProcess{
		MeetingID: config.MeetingID,
		Port:      port,
		PID:       cmd.Process.Pid,
		Status:    StatusConnecting,
		cmd:       cmd,
		cancel:    cancel,
		stopChan:  make(chan struct{}),
		config:    config,
		Stats: MeetingStats{
			StartTime: time.Now(),
		},
	}

	// Monitor process in background
	go pm.monitorWorker(worker)
	if err := pm.waitForWorkerReady(worker, 10*time.Second); err != nil {
		worker.Stop()
		return nil, fmt.Errorf("worker failed to become ready: %w", err)
	}

	worker.Status = StatusInMeeting

	return worker, nil
}

func (pm *ProcessManager) waitForWorkerReady(worker *WorkerProcess, timeout time.Duration) error {
	deadline := time.Now().Add(timeout)
	client := &http.Client{Timeout: 1 * time.Second}

	for time.Now().Before(deadline) {
		// Try to connect to worker health endpoint
		url := fmt.Sprintf("http://localhost:%d/health", worker.Port)
		resp, err := client.Get(url)
		if err == nil {
			resp.Body.Close()
			if resp.StatusCode == http.StatusOK {
				log.Infof("Worker ready: %s (PID: %d)", worker.MeetingID, worker.PID)
				return nil
			}
		}

		time.Sleep(200 * time.Millisecond)
	}

	return fmt.Errorf("worker did not become ready within timeout")
}

func (pm *ProcessManager) monitorWorker(worker *WorkerProcess) {
	err := worker.cmd.Wait()

	if err != nil {
		log.Errorf("Worker process for meeting %s exited with error: %v", worker.MeetingID, err)
		worker.Status = StatusFailed
	} else {
		log.Infof("Worker process for meeting %s exited normally", worker.MeetingID)
		worker.Status = StatusIdle
	}

	// Clean up
	close(worker.stopChan)
	pm.workers.Delete(worker.MeetingID)
	pm.usedPorts.Delete(worker.Port)
}

func (pm *ProcessManager) streamAudioFromWorker(worker *WorkerProcess) {
	// Wait a bit for the worker to fully start
	time.Sleep(1 * time.Second)

	url := fmt.Sprintf("http://localhost:%d/audio", worker.Port)

	// Create cancellable request
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go func() {
		<-worker.stopChan
		cancel()
	}()

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		log.Errorf("Failed to create audio stream request: %v", err)
		return
	}

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		log.Errorf("Failed to connect to worker audio stream: %v", err)
		return
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		log.Errorf("Worker audio stream returned status: %d", resp.StatusCode)
		return
	}

	log.Infof("Audio streaming started from worker: %s", worker.MeetingID)

	// Read frames line by line (newline-delimited JSON)
	scanner := bufio.NewScanner(resp.Body)
	scanner.Buffer(make([]byte, 1024*1024), 1024*1024)

	for scanner.Scan() {
		select {
		case <-worker.stopChan:
			log.Infof("Stopping audio stream for worker: %s", worker.MeetingID)
			return
		default:
		}

		line := scanner.Bytes()
		if len(line) == 0 {
			continue
		}

		var frame audio.AudioFrame
		if err := json.Unmarshal(line, &frame); err != nil {
			log.Errorf("Failed to unmarshal audio frame: %v", err)
			continue
		}

		// Publish to audio bus
		published := true
		if pm.audioBus != nil {
			published = pm.audioBus.Publish(worker.MeetingID, &frame)
		}

		// Update statistics
		worker.statsMux.Lock()
		worker.Stats.FramesReceived++
		worker.Stats.BytesReceived += uint64(len(frame.Data))
		worker.Stats.LastFrameTime = time.Now()
		if !published {
			worker.Stats.FramesDropped++
		}
		worker.statsMux.Unlock()
	}

	if err := scanner.Err(); err != nil && err != io.EOF {
		log.Errorf("Error reading audio stream: %v", err)
	}

	log.Infof("Audio streaming ended for worker: %s", worker.MeetingID)
}

// LeaveMeeting stops a worker process
func (pm *ProcessManager) LeaveMeeting(meetingID string) error {
	value, exists := pm.workers.LoadAndDelete(meetingID)
	if !exists {
		return fmt.Errorf("meeting %s not found", meetingID)
	}

	worker := value.(*WorkerProcess)
	pm.usedPorts.Delete(worker.Port)

	if err := worker.Stop(); err != nil {
		log.Errorf("Error stopping worker %s: %v", meetingID, err)
	}

	log.Infof("Successfully stopped worker for meeting: %s", meetingID)
	return nil
}

func (pm *ProcessManager) GetMeeting(meetingID string) (*WorkerProcess, bool) {
	value, exists := pm.workers.Load(meetingID)
	if !exists {
		return nil, false
	}
	return value.(*WorkerProcess), true
}

func (pm *ProcessManager) ListMeetings() map[string]MeetingStatus {
	result := make(map[string]MeetingStatus)
	pm.workers.Range(func(key, value interface{}) bool {
		id := key.(string)
		worker := value.(*WorkerProcess)
		result[id] = worker.Status
		return true
	})
	return result
}

func (pm *ProcessManager) GetMeetingStats(meetingID string) (*MeetingStats, error) {
	value, exists := pm.workers.Load(meetingID)
	if !exists {
		return nil, fmt.Errorf("meeting %s not found", meetingID)
	}

	worker := value.(*WorkerProcess)
	worker.statsMux.RLock()
	stats := worker.Stats
	worker.statsMux.RUnlock()
	return &stats, nil
}

func (pm *ProcessManager) GetAllStats() map[string]MeetingStats {
	result := make(map[string]MeetingStats)
	pm.workers.Range(func(key, value interface{}) bool {
		id := key.(string)
		worker := value.(*WorkerProcess)
		worker.statsMux.RLock()
		result[id] = worker.Stats
		worker.statsMux.RUnlock()
		return true
	})
	return result
}

// Shutdown gracefully shuts down all worker processes
func (pm *ProcessManager) Shutdown() error {
	log.Info("Shutting down process manager")

	pm.workers.Range(func(key, value interface{}) bool {
		id := key.(string)
		worker := value.(*WorkerProcess)
		log.Infof("Stopping worker: %s (PID: %d)", id, worker.PID)
		if err := worker.Stop(); err != nil {
			log.Errorf("Error stopping worker %s: %v", id, err)
		}
		pm.workers.Delete(id)
		pm.usedPorts.Delete(worker.Port)
		return true
	})

	log.Info("Process manager shutdown complete")
	return nil
}

func (pm *ProcessManager) GetMeetingCount() int {
	count := 0
	pm.workers.Range(func(_, _ interface{}) bool {
		count++
		return true
	})
	return count
}

func (w *WorkerProcess) Stop() error {
	if w.stopped {
		return nil
	}

	log.Infof("Stopping worker process: %s (PID: %d)", w.MeetingID, w.PID)
	w.Status = StatusEnded
	w.stopped = true

	// Send SIGTERM to worker
	if w.cmd != nil && w.cmd.Process != nil {
		if err := w.cmd.Process.Signal(syscall.SIGTERM); err != nil {
			log.Warnf("Failed to send SIGTERM to worker: %v", err)
		}
	}

	// Cancel context (which will eventually SIGKILL via CommandContext)
	if w.cancel != nil {
		w.cancel()
	}

	return nil
}
