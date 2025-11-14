package main

import (
	"encoding/gob"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/stream"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/zoombot"
	. "github.com/qieqieplus/headless-meeting-bot/server/pkg/zoombot"
)

const (
	basePort            = 9000
	maxPort             = 9999
	workerReadyTimeout  = 10 * time.Second
	workerStopTimeout   = 3 * time.Second
	healthCheckTimeout  = 1 * time.Second
	healthCheckInterval = 200 * time.Millisecond
	stateRequestTimeout = 2 * time.Second
)

// ProcessManager manages meeting worker processes
type ProcessManager struct {
	workers     sync.Map // map[string]*WorkerProcess
	audioBus    *stream.AudioBus
	eventsBus   *stream.EventBus
	videoBus    *stream.VideoBus
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
	cmd       *exec.Cmd
	stopChan  chan struct{}
	stopped   bool
	config    *MeetingConfig
}

// Stop terminates the worker process
func (w *WorkerProcess) Stop() error {
	if w.stopped {
		return nil
	}

	log.Infof("Stopping worker process: %s (PID: %d)", w.MeetingID, w.PID)
	w.Status = zoombot.StatusEnded
	w.stopped = true

	if w.cmd == nil || w.cmd.Process == nil {
		return nil
	}

	// Try graceful HTTP shutdown first
	client := &http.Client{Timeout: workerStopTimeout}
	url := fmt.Sprintf("http://localhost:%d/shutdown", w.Port)
	req, _ := http.NewRequest(http.MethodPost, url, nil)
	_, _ = client.Do(req)

	// Wait for monitorWorker to reap and close stopChan
	if w.waitForStop(workerStopTimeout) {
		return nil
	}

	// Escalate to SIGTERM
	if err := w.cmd.Process.Signal(syscall.SIGTERM); err != nil {
		log.Warnf("Failed to send SIGTERM to worker: %v", err)
	}

	if w.waitForStop(workerStopTimeout * 2) {
		return nil
	}

	// Last resort: SIGKILL
	log.Warnf("Worker %s did not exit after SIGTERM; sending SIGKILL", w.MeetingID)
	_ = w.cmd.Process.Signal(syscall.SIGKILL)
	return nil
}

// waitForStop waits for stopChan to close, returns true if stopped within timeout
func (w *WorkerProcess) waitForStop(timeout time.Duration) bool {
	select {
	case <-w.stopChan:
		return true
	case <-time.After(timeout):
		return false
	}
}

func NewProcessManager(sdkKey, sdkSecret string, audioBus *stream.AudioBus, eventsBus *stream.EventBus, videoBus *stream.VideoBus) (*ProcessManager, error) {
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
		eventsBus: eventsBus,
		videoBus:  videoBus,
		sdkKey:    sdkKey,
		sdkSecret: sdkSecret,
		basePort:  basePort,
		maxPort:   maxPort,
		workerBin: workerBin,
	}, nil
}

func (pm *ProcessManager) JoinMeeting(meetingID, password, displayName, joinToken string, enableAudio, enableVideo bool) error {
	if _, exists := pm.workers.Load(meetingID); exists {
		return zoombot.ErrMeetingAlreadyExists
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

	// Start streaming from worker
	go pm.streamAudioFromWorker(worker)
	go pm.streamEventsFromWorker(worker)
	go pm.streamVideoFromWorker(worker)

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

// spawnWorker creates and starts a new worker process
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

	// Create command - use the same binary with "worker" subcommand
	cmd := exec.Command(pm.workerBin, "worker", "-config", string(configJSON))
	// Enable rich crash diagnostics for the worker process
	cmd.Env = append(os.Environ(),
		"GOTRACEBACK=crash", // full goroutine + native frames, core dump if possible
	)
	cmd.Stdout = os.Stdout // Forward stdout for debugging
	cmd.Stderr = os.Stderr // Forward stderr for debugging

	// Ensure worker is killed if parent dies unexpectedly
	cmd.SysProcAttr = &syscall.SysProcAttr{
		Pdeathsig: syscall.SIGKILL,
	}

	// Start the process
	if err := cmd.Start(); err != nil {
		return nil, fmt.Errorf("failed to start worker process: %w", err)
	}

	worker := &WorkerProcess{
		MeetingID: config.MeetingID,
		Port:      port,
		PID:       cmd.Process.Pid,
		Status:    zoombot.StatusConnecting,
		cmd:       cmd,
		stopChan:  make(chan struct{}),
		config:    config,
	}

	// Monitor process in background
	go pm.monitorWorker(worker)
	if err := pm.waitForWorkerReady(worker, workerReadyTimeout); err != nil {
		worker.Stop()
		return nil, fmt.Errorf("worker failed to become ready: %w", err)
	}

	worker.Status = zoombot.StatusInMeeting

	return worker, nil
}

// waitForWorkerReady waits for the worker to become ready
func (pm *ProcessManager) waitForWorkerReady(worker *WorkerProcess, timeout time.Duration) error {
	deadline := time.Now().Add(timeout)
	client := &http.Client{Timeout: healthCheckTimeout}
	url := fmt.Sprintf("http://localhost:%d/health", worker.Port)

	for time.Now().Before(deadline) {
		resp, err := client.Get(url)
		if err == nil {
			resp.Body.Close()
			if resp.StatusCode == http.StatusOK {
				log.Infof("Worker ready: %s (PID: %d)", worker.MeetingID, worker.PID)
				return nil
			}
		}
		time.Sleep(healthCheckInterval)
	}

	return fmt.Errorf("worker did not become ready within timeout")
}

// monitorWorker monitors the worker process and updates status
func (pm *ProcessManager) monitorWorker(worker *WorkerProcess) {
	err := worker.cmd.Wait()

	if err != nil {
		// Provide detailed exit diagnostics (exit code, signal, core dumped)
		if exitErr, ok := err.(*exec.ExitError); ok {
			if status, ok := exitErr.Sys().(syscall.WaitStatus); ok {
				if status.Signaled() {
					log.Errorf("Worker %s crashed: signal=%s core=%v", worker.MeetingID, status.Signal(), status.CoreDump())
				} else {
					log.Errorf("Worker %s exited with status=%d", worker.MeetingID, status.ExitStatus())
				}
			} else {
				log.Errorf("Worker %s exited with error (unknown status): %v", worker.MeetingID, err)
			}
		} else {
			log.Errorf("Worker process for meeting %s exited with error: %v", worker.MeetingID, err)
		}
		worker.Status = zoombot.StatusFailed
	} else {
		log.Infof("Worker process for meeting %s exited normally", worker.MeetingID)
		worker.Status = zoombot.StatusIdle
	}

	// Clean up
	close(worker.stopChan)
	pm.workers.Delete(worker.MeetingID)
	pm.usedPorts.Delete(worker.Port)
}

// GetMeeting returns a worker process by meeting ID
func (pm *ProcessManager) GetMeeting(meetingID string) (*WorkerProcess, bool) {
	value, exists := pm.workers.Load(meetingID)
	if !exists {
		return nil, false
	}
	return value.(*WorkerProcess), true
}

func (pm *ProcessManager) LeaveMeeting(meetingID string) error {
	value, exists := pm.workers.LoadAndDelete(meetingID)
	if !exists {
		return zoombot.ErrMeetingNotFound
	}

	worker := value.(*WorkerProcess)
	pm.usedPorts.Delete(worker.Port)

	if err := worker.Stop(); err != nil {
		log.Errorf("Error stopping worker %s: %v", meetingID, err)
	}

	log.Infof("Successfully stopped worker for meeting: %s", meetingID)
	return nil
}

func (pm *ProcessManager) ListMeetings() map[string]zoombot.StatusInfo {
	result := make(map[string]zoombot.StatusInfo)
	pm.workers.Range(func(key, value interface{}) bool {
		id := key.(string)
		worker := value.(*WorkerProcess)
		// Default to local cached status; upgrade with worker-reported StatusInfo if available
		info := zoombot.StatusInfo{
			State:  worker.Status.String(),
			Detail: 0,
		}
		if state, err := pm.requestWorkerState(worker, zoombot.StateStatus); err == nil {
			info = state.Status
		} else {
			log.Warnf("ListMeetings: failed to fetch status from worker %s: %v", id, err)
		}
		result[id] = info
		return true
	})
	return result
}

// Status returns the status for a specific meeting.
func (pm *ProcessManager) Status(meetingID string) (zoombot.StatusInfo, error) {
	state, err := pm.State(meetingID, zoombot.StateStatus)
	if err != nil {
		return zoombot.StatusInfo{}, err
	}
	return state.Status, nil
}

// Statistics returns the statistics for a specific meeting.
func (pm *ProcessManager) Statistics(meetingID string) (zoombot.MeetingStatistics, error) {
	state, err := pm.State(meetingID, zoombot.StateStatistics)
	if err != nil {
		return zoombot.MeetingStatistics{}, err
	}
	return state.Statistics, nil
}

// Users returns the list of users for a specific meeting.
func (pm *ProcessManager) Users(meetingID string) ([]stream.UserInfo, error) {
	state, err := pm.State(meetingID, zoombot.StateUsers)
	if err != nil {
		return nil, err
	}
	return state.Users, nil
}

// State returns a bundle of meeting data selected by mask.
func (pm *ProcessManager) State(meetingID string, mask zoombot.StateMask) (zoombot.MeetingState, error) {
	value, exists := pm.workers.Load(meetingID)
	if !exists {
		return zoombot.MeetingState{}, zoombot.ErrMeetingNotFound
	}

	return pm.requestWorkerState(value.(*WorkerProcess), mask)
}

func (pm *ProcessManager) requestWorkerState(worker *WorkerProcess, mask zoombot.StateMask) (zoombot.MeetingState, error) {
	client := &http.Client{Timeout: stateRequestTimeout}
	url := fmt.Sprintf("http://localhost:%d/state?mask=%d", worker.Port, mask)

	resp, err := client.Get(url)
	if err != nil {
		return zoombot.MeetingState{}, fmt.Errorf("request worker state: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return zoombot.MeetingState{}, fmt.Errorf("worker responded with %s", resp.Status)
	}

	var state zoombot.MeetingState
	if err := gob.NewDecoder(resp.Body).Decode(&state); err != nil {
		return zoombot.MeetingState{}, fmt.Errorf("decode worker state: %w", err)
	}

	return state, nil
}

// Shutdown gracefully shuts down all worker processes
func (pm *ProcessManager) Shutdown() error {
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
