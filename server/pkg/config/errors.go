package config

import "errors"

var (
	ErrMissingSDKKey    = errors.New("zoom SDK key is required (set ZOOM_SDK_KEY env var or --sdk-key flag)")
	ErrMissingSDKSecret = errors.New("zoom SDK secret is required (set ZOOM_SDK_SECRET env var or --sdk-secret flag)")
)
