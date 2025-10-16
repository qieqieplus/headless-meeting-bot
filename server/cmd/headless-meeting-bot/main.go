package main

import (
	"flag"
	"fmt"
	"os"

	"github.com/qieqieplus/headless-meeting-bot/server/pkg/log"
)

func main() {
	if len(os.Args) < 2 {
		printUsage()
		os.Exit(1)
	}

	command := os.Args[1]

	switch command {
	case "server":
		runServer(os.Args[2:])
	case "worker":
		runWorker(os.Args[2:])
	default:
		fmt.Fprintf(os.Stderr, "Unknown command: %s\n", command)
		printUsage()
		os.Exit(1)
	}
}

func printUsage() {
	fmt.Fprintf(os.Stderr, `Usage: %s <command> [options]

Commands:
  server    Start the main HTTP/WebSocket server
  worker    Start a meeting worker process

Run '%s <command> -h' for more information on a command.
`, os.Args[0], os.Args[0])
}

func runServer(args []string) {
	fs := flag.NewFlagSet("server", flag.ExitOnError)
	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage: %s server [options]\n\nStarts the main HTTP/WebSocket server.\n\nOptions:\n", os.Args[0])
		fs.PrintDefaults()
	}

	if err := fs.Parse(args); err != nil {
		log.Fatalf("Failed to parse flags: %v", err)
	}

	// Run the server
	startServer()
}

func runWorker(args []string) {
	fs := flag.NewFlagSet("worker", flag.ExitOnError)
	configJSON := fs.String("config", "", "JSON configuration for the worker")
	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage: %s worker [options]\n\nStarts a meeting worker process.\n\nOptions:\n", os.Args[0])
		fs.PrintDefaults()
	}

	if err := fs.Parse(args); err != nil {
		log.Fatalf("Failed to parse flags: %v", err)
	}

	if *configJSON == "" {
		log.Fatal("No configuration provided. Use -config flag with JSON config")
	}

	// Run the worker
	startWorker(*configJSON)
}
