package config

import (
	"flag"
	"os"
	"strconv"
	"time"
)

// WebSocketConfig holds WebSocket-specific configuration
type WebSocketConfig struct {
	WriteTimeout       time.Duration // Timeout for writing messages to WebSocket
	ReadTimeout        time.Duration // Timeout for reading messages from WebSocket (keepalive)
	PingInterval       time.Duration // Interval for sending ping messages
	AudioFlushInterval time.Duration // Interval for flushing aggregated audio frames
}

type Config struct {
	// Zoom SDK credentials
	ZoomSDKKey    string
	ZoomSDKSecret string

	// Server configuration
	HTTPAddr string
	LogLevel string

	// Audio configuration
	AudioSampleRate int
	AudioChannels   int

	// WebSocket configuration
	WebSocket WebSocketConfig
}

func Load() *Config {
	cfg := &Config{
		HTTPAddr:        ":8080",
		LogLevel:        "info",
		AudioSampleRate: 32000,
		AudioChannels:   1,

		// WebSocket defaults
		WebSocket: WebSocketConfig{
			WriteTimeout:       5 * time.Second,
			ReadTimeout:        3 * time.Minute,
			PingInterval:       60 * time.Second,
			AudioFlushInterval: 100 * time.Millisecond,
		},
	}

	// Load from environment variables
	if key := os.Getenv("ZOOM_SDK_KEY"); key != "" {
		cfg.ZoomSDKKey = key
	}
	if secret := os.Getenv("ZOOM_SDK_SECRET"); secret != "" {
		cfg.ZoomSDKSecret = secret
	}
	if addr := os.Getenv("HTTP_ADDR"); addr != "" {
		cfg.HTTPAddr = addr
	}
	if level := os.Getenv("LOG_LEVEL"); level != "" {
		cfg.LogLevel = level
	}
	if sampleRate := os.Getenv("AUDIO_SAMPLE_RATE"); sampleRate != "" {
		if rate, err := strconv.Atoi(sampleRate); err == nil {
			cfg.AudioSampleRate = rate
		}
	}
	if channels := os.Getenv("AUDIO_CHANNELS"); channels != "" {
		if ch, err := strconv.Atoi(channels); err == nil {
			cfg.AudioChannels = ch
		}
	}

	// WebSocket configuration from environment variables (timeout values in seconds)
	if timeout := os.Getenv("WEBSOCKET_WRITE_TIMEOUT"); timeout != "" {
		if seconds, err := strconv.Atoi(timeout); err == nil {
			cfg.WebSocket.WriteTimeout = time.Duration(seconds) * time.Second
		}
	}
	if timeout := os.Getenv("WEBSOCKET_READ_TIMEOUT"); timeout != "" {
		if seconds, err := strconv.Atoi(timeout); err == nil {
			cfg.WebSocket.ReadTimeout = time.Duration(seconds) * time.Second
		}
	}
	if interval := os.Getenv("WEBSOCKET_PING_INTERVAL"); interval != "" {
		if seconds, err := strconv.Atoi(interval); err == nil {
			cfg.WebSocket.PingInterval = time.Duration(seconds) * time.Second
		}
	}
	if interval := os.Getenv("WEBSOCKET_AUDIO_FLUSH_INTERVAL"); interval != "" {
		if ms, err := strconv.Atoi(interval); err == nil {
			cfg.WebSocket.AudioFlushInterval = time.Duration(ms) * time.Millisecond
		}
	}

	// Parse command line flags
	flag.StringVar(&cfg.HTTPAddr, "http", cfg.HTTPAddr, "HTTP server address")
	flag.StringVar(&cfg.LogLevel, "log-level", cfg.LogLevel, "Log level (debug, info, warn, error)")
	flag.StringVar(&cfg.ZoomSDKKey, "sdk-key", cfg.ZoomSDKKey, "Zoom SDK key")
	flag.StringVar(&cfg.ZoomSDKSecret, "sdk-secret", cfg.ZoomSDKSecret, "Zoom SDK secret")
	flag.Parse()

	return cfg
}

func (c *Config) Validate() error {
	if c.ZoomSDKKey == "" {
		return ErrMissingSDKKey
	}
	if c.ZoomSDKSecret == "" {
		return ErrMissingSDKSecret
	}
	return nil
}
