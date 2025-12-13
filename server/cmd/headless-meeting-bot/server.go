package main

import (
	"context"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/config"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/manifest"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/server"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/server/ws"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/stream"
)

func startServer() {
	cfg := config.Load()
	if err := cfg.Validate(); err != nil {
		log.Fatal(err)
	}

	log.Init(cfg.LogLevel)
	log.Info("Starting server...")

	audioBus := stream.NewAudioBus()
	eventsBus := stream.NewEventBus()
	videoBus := stream.NewVideoBus()

	tracker := manifest.NewInMemoryTracker(manifest.Options{
		AudioFormat: manifest.AudioFormat{
			Encoding:   "S16LE",
			SampleRate: cfg.AudioSampleRate,
			Channels:   cfg.AudioChannels,
		},
	})

	// Use ProcessManager for multi-process architecture to avoid GLib context conflicts
	processManager, err := NewProcessManager(cfg.ZoomSDKKey, cfg.ZoomSDKSecret, audioBus, eventsBus, videoBus, tracker)
	if err != nil {
		log.Fatalf("Failed to create process manager: %v", err)
	}

	wsServer := ws.NewWebSocketServer(audioBus, eventsBus, videoBus, processManager, cfg)
	httpServer := server.NewHTTPServer(processManager, wsServer, tracker)

	srv := &http.Server{
		Addr:    cfg.HTTPAddr,
		Handler: httpServer,
	}

	go func() {
		log.Infof("HTTP server listening on %s", cfg.HTTPAddr)
		if err := srv.ListenAndServe(); err != http.ErrServerClosed {
			log.Fatalf("HTTP server error: %v", err)
		}
	}()

	waitForShutdown(srv, processManager)
}

func waitForShutdown(srv *http.Server, manager *ProcessManager) {
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGINT, syscall.SIGTERM)

	<-stop

	log.Info("Shutting down server...")

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	if err := manager.Shutdown(); err != nil {
		log.Errorf("Error during meeting manager shutdown: %v", err)
	}

	if err := srv.Shutdown(ctx); err != nil {
		log.Errorf("Error during HTTP server shutdown: %v", err)
	}

	log.Info("Server shutdown complete.")
}
