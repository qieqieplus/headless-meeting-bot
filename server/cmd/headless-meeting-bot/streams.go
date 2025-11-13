package main

import (
	"encoding/gob"
	"fmt"
	"io"
	"net/http"
	"time"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/stream"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/zoombot"
)

const (
	minBackoff = 1 * time.Second
	maxBackoff = 30 * time.Second
)

// streamFromWorker handles the common logic for streaming data from a worker process.
// It manages connection, retry with exponential backoff, and delegates to a processor.
func (pm *ProcessManager) streamFromWorker(worker *WorkerProcess, endpoint string, processor func(*WorkerProcess, *http.Response)) {
	client := &http.Client{}
	url := fmt.Sprintf("http://localhost:%d%s", worker.Port, endpoint)
	backoff := minBackoff

	for {
		// Early return if worker is stopped to avoid retry spam during shutdown
		if worker.stopped {
			log.Infof("Worker %s stopped, ending %s stream", worker.MeetingID, endpoint)
			return
		}

		select {
		case <-worker.stopChan:
			log.Infof("Stopping %s stream for worker: %s", endpoint, worker.MeetingID)
			return
		default:
		}

		time.Sleep(backoff)

		req, err := http.NewRequest(http.MethodGet, url, nil)
		if err != nil {
			log.Errorf("Failed to create %s stream request for %s: %v", endpoint, worker.MeetingID, err)
			backoff = updateBackoff(backoff)
			continue
		}

		resp, err := client.Do(req)
		if err != nil {
			log.Warnf("Failed to connect to worker %s stream for %s (retrying): %v", endpoint, worker.MeetingID, err)
			backoff = updateBackoff(backoff)
			continue
		}

		if resp.StatusCode != http.StatusOK {
			resp.Body.Close()
			log.Warnf("Worker %s stream for %s returned status %d (retrying)", endpoint, worker.MeetingID, resp.StatusCode)
			backoff = updateBackoff(backoff)
			continue
		}

		backoff = minBackoff
		log.Infof("%s streaming started from worker: %s", endpoint, worker.MeetingID)

		processor(worker, resp)
		resp.Body.Close()

		log.Warnf("%s stream connection lost for worker %s, retrying", endpoint, worker.MeetingID)
	}
}

func updateBackoff(current time.Duration) time.Duration {
	if current < maxBackoff {
		return current * 2
	}
	return maxBackoff
}

// processGOBStream is a generic helper for processing GOB-encoded streams from workers
func (pm *ProcessManager) processGOBStream(
	worker *WorkerProcess,
	resp *http.Response,
	streamType string,
	processItem func(*gob.Decoder) error,
) {
	dec := gob.NewDecoder(resp.Body)
	for {
		select {
		case <-worker.stopChan:
			return
		default:
		}

		if err := processItem(dec); err != nil {
			if err != io.EOF {
				log.Errorf("Error reading %s stream from %s: %v", streamType, worker.MeetingID, err)
			}
			return
		}
	}
}

// mapEventStatusToMeetingStatus converts event status string to MeetingStatus
func (pm *ProcessManager) mapEventStatusToMeetingStatus(status string) zoombot.MeetingStatus {
	switch status {
	case "idle":
		return zoombot.StatusIdle
	case "connecting":
		return zoombot.StatusConnecting
	case "in_meeting":
		return zoombot.StatusInMeeting
	case "reconnecting":
		return zoombot.StatusReconnecting
	case "failed":
		return zoombot.StatusFailed
	case "ended":
		return zoombot.StatusEnded
	default:
		return zoombot.StatusUnknown
	}
}

// streamAudioFromWorker streams audio frames from a worker process to the audio bus
func (pm *ProcessManager) streamAudioFromWorker(worker *WorkerProcess) {
	pm.streamFromWorker(worker, "/audio", pm.processAudioStream)
}

// processAudioStream processes the GOB stream from the worker
func (pm *ProcessManager) processAudioStream(worker *WorkerProcess, resp *http.Response) {
	pm.processGOBStream(worker, resp, "audio", func(dec *gob.Decoder) error {
		var frame stream.AudioEvent
		if err := dec.Decode(&frame); err != nil {
			return err
		}
		if pm.audioBus != nil {
			pm.audioBus.Publish(worker.MeetingID, &frame)
		}
		return nil
	})
}

// streamEventsFromWorker streams events from a worker process to the events bus
func (pm *ProcessManager) streamEventsFromWorker(worker *WorkerProcess) {
	pm.streamFromWorker(worker, "/events", pm.processEventsStream)
}

// processEventsStream processes the GOB stream from the worker
func (pm *ProcessManager) processEventsStream(worker *WorkerProcess, resp *http.Response) {
	pm.processGOBStream(worker, resp, "events", func(dec *gob.Decoder) error {
		var event stream.Event
		if err := dec.Decode(&event); err != nil {
			return err
		}

		// Keep ProcessManager's view of meeting status in sync with worker-emitted status
		if event.Type == stream.EventTypeMeetingStatus {
			worker.Status = pm.mapEventStatusToMeetingStatus(event.Status)
		}

		if pm.eventsBus != nil {
			pm.eventsBus.Publish(event.MeetingID, &event)
		}
		return nil
	})
}

// streamVideoFromWorker streams video files from a worker process to the video bus
func (pm *ProcessManager) streamVideoFromWorker(worker *WorkerProcess) {
	pm.streamFromWorker(worker, "/video", pm.processVideoStream)
}

// processVideoStream processes the GOB stream from the worker
func (pm *ProcessManager) processVideoStream(worker *WorkerProcess, resp *http.Response) {
	pm.processGOBStream(worker, resp, "video", func(dec *gob.Decoder) error {
		var fileEvent stream.FileEvent
		if err := dec.Decode(&fileEvent); err != nil {
			return err
		}

		if pm.videoBus != nil {
			if !pm.videoBus.Publish(fileEvent.MeetingID, &fileEvent) {
				log.Warnf("[video] published but no active subscribers: meeting=%s file=%s", fileEvent.MeetingID, fileEvent.Filename)
			}
		} else {
			log.Warnf("[video] videoBus is nil; dropping file: %s", fileEvent.Filename)
		}
		return nil
	})
}
