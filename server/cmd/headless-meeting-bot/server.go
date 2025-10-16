package main

import (
	"context"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/audio"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/config"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
	"github.com/qieqieplus/headless-meeting-bot/server/pkg/server"
)

func startServer() {
	// Load configuration
	cfg := config.Load()
	if err := cfg.Validate(); err != nil {
		log.Fatal(err)
	}

	// Initialize logger
	log.Init(cfg.LogLevel)
	log.Info("Starting server...")

	// Create components
	audioBus := audio.NewBus()

	// Use ProcessManager for multi-process architecture to avoid GLib context conflicts
	processManager, err := NewProcessManager(cfg.ZoomSDKKey, cfg.ZoomSDKSecret, audioBus)
	if err != nil {
		log.Fatalf("Failed to create process manager: %v", err)
	}

	wsServer := server.NewWebSocketServer(audioBus, processManager, cfg)
	httpServer := server.NewHTTPServer(processManager, wsServer)

	// Start HTTP server in a goroutine
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

	// Wait for shutdown signal
	waitForShutdown(srv, processManager)
}

func waitForShutdown(srv *http.Server, manager *ProcessManager) {
	// Create channel to listen for OS signals
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGINT, syscall.SIGTERM)

	// Block until a signal is received
	<-stop

	log.Info("Shutting down server...")

	// Create a context with a timeout for graceful shutdown
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	// Shutdown meeting manager first
	if err := manager.Shutdown(); err != nil {
		log.Errorf("Error during meeting manager shutdown: %v", err)
	} else {
		log.Info("Meeting manager shut down successfully")
	}

	// Shutdown HTTP server
	if err := srv.Shutdown(ctx); err != nil {
		log.Errorf("Error during HTTP server shutdown: %v", err)
	} else {
		log.Info("HTTP server shut down successfully")
	}

	log.Info("Server shutdown complete.")
}
