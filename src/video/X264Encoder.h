#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>
#include <string>

// Forward declare x264 types to avoid including headers here
struct x264_t;
struct x264_picture_t;
struct x264_param_t;

struct X264EncoderConfig {
	int width = 0;
	int height = 0;
	int fps = 30;
	int bitrateKbps = 1500;
	int keyint = 60; // IDR interval in frames
	std::string preset = "veryfast";
	std::string tune = "zerolatency";
	std::string profile = "high";
	std::string level = ""; // empty = auto
};

struct EncodedAccessUnit {
	std::vector<uint8_t> bytes; // Annex B access unit for a single frame
	bool isKeyframe = false;
	unsigned long long timestamp = 0; // ms
};

class X264Encoder {
public:
	X264Encoder();
	~X264Encoder();

	bool initialize(const X264EncoderConfig& config);
	bool reinitialize(const X264EncoderConfig& config);
	void shutdown();

	// Input planes are I420 with contiguous rows and standard strides: Y=W, U=V=W/2
	bool encodeI420(const uint8_t* yPlane,
	               const uint8_t* uPlane,
	               const uint8_t* vPlane,
	               int width,
	               int height,
	               unsigned long long timestampMs,
	               EncodedAccessUnit& outAu);

	void requestIDR();

	int getWidth() const { return currentConfig.width; }
	int getHeight() const { return currentConfig.height; }
	int getFps() const { return currentConfig.fps; }

private:
	bool openEncoder();
	void closeEncoder();
	bool configureParams(x264_param_t& param);
	bool readEncoderOutput(EncodedAccessUnit& outAu, unsigned long long ts);

private:
	X264EncoderConfig currentConfig;
	x264_t* encoder = nullptr;
	std::unique_ptr<x264_picture_t> picIn; // reused input picture
	int forceIdr = 0;
};


