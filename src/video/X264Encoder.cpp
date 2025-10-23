#include "X264Encoder.h"

#include <cstring>
#include <cassert>
#include <vector>
#include <iostream>

extern "C" {
#include <x264.h>
}

X264Encoder::X264Encoder() = default;
X264Encoder::~X264Encoder() { shutdown(); }

bool X264Encoder::initialize(const X264EncoderConfig& config) {
	currentConfig = config;
	return openEncoder();
}

bool X264Encoder::reinitialize(const X264EncoderConfig& config) {
	shutdown();
	currentConfig = config;
	return openEncoder();
}

void X264Encoder::shutdown() {
	closeEncoder();
}

bool X264Encoder::openEncoder() {
	if (currentConfig.width <= 0 || currentConfig.height <= 0 || currentConfig.fps <= 0) return false;

	x264_param_t param;
	if (x264_param_default_preset(&param, currentConfig.preset.c_str(), currentConfig.tune.c_str()) < 0) {
		return false;
	}

	param.i_width = currentConfig.width;
	param.i_height = currentConfig.height;
	param.i_csp = X264_CSP_I420;
	param.i_fps_num = currentConfig.fps;
	param.i_fps_den = 1;
	param.i_timebase_num = 1;
	param.i_timebase_den = currentConfig.fps; // PTS in frames

	param.i_keyint_max = currentConfig.keyint > 0 ? currentConfig.keyint : currentConfig.fps * 2;
	param.i_keyint_min = param.i_keyint_max / 2;
	param.b_intra_refresh = 0;

	// Rate control
	param.rc.i_bitrate = currentConfig.bitrateKbps;
	param.rc.i_vbv_max_bitrate = currentConfig.bitrateKbps;
	param.rc.i_vbv_buffer_size = currentConfig.bitrateKbps * 2;

	// Low latency
	param.i_bframe = 0;
	param.b_vfr_input = 0;
	param.b_repeat_headers = 1; // emit SPS/PPS on IDR
	param.b_annexb = 1; // produce start codes

	// Threads
	param.i_threads = 0; // auto

	if (!configureParams(param)) return false;

	encoder = x264_encoder_open(&param);
	if (!encoder) return false;

	picIn.reset(new x264_picture_t());
	x264_picture_init(picIn.get());
	if (x264_picture_alloc(picIn.get(), X264_CSP_I420, currentConfig.width, currentConfig.height) != 0) {
		closeEncoder();
		return false;
	}
	return true;
}

void X264Encoder::closeEncoder() {
	if (picIn) {
		x264_picture_clean(picIn.get());
		picIn.reset();
	}
	if (encoder) {
		x264_encoder_close(encoder);
		encoder = nullptr;
	}
}

bool X264Encoder::configureParams(x264_param_t& param) {
	if (!currentConfig.profile.empty()) {
		if (x264_param_apply_profile(&param, currentConfig.profile.c_str()) < 0) return false;
	}
	// Level is typically applied via profile string; leaving as-is for now.
	return true;
}

bool X264Encoder::encodeI420(const uint8_t* yPlane,
							 const uint8_t* uPlane,
							 const uint8_t* vPlane,
							 int width,
							 int height,
							 unsigned long long timestampMs,
							 EncodedAccessUnit& outAu) {
	if (!encoder || !picIn) return false;
	if (width != currentConfig.width || height != currentConfig.height) return false;

	// Copy planar data into x264 picture
	int yStride = picIn->img.i_stride[0];
	int uStride = picIn->img.i_stride[1];
	int vStride = picIn->img.i_stride[2];

	const uint8_t* srcY = yPlane;
	const uint8_t* srcU = uPlane;
	const uint8_t* srcV = vPlane;
	uint8_t* dstY = picIn->img.plane[0];
	uint8_t* dstU = picIn->img.plane[1];
	uint8_t* dstV = picIn->img.plane[2];

	for (int r = 0; r < height; ++r) {
		std::memcpy(dstY + r * yStride, srcY + r * width, width);
	}
	int chromaH = height / 2;
	int chromaW = width / 2;
	for (int r = 0; r < chromaH; ++r) {
		std::memcpy(dstU + r * uStride, srcU + r * chromaW, chromaW);
		std::memcpy(dstV + r * vStride, srcV + r * chromaW, chromaW);
	}

	// Convert ms timestamp to frame-based PTS
	// For integer fps, approximate pts = round(ms * fps / 1000)
	uint64_t pts = (timestampMs * static_cast<uint64_t>(currentConfig.fps) + 500) / 1000;
	picIn->i_pts = static_cast<int64_t>(pts);

	if (forceIdr) {
		picIn->i_type = X264_TYPE_IDR;
		forceIdr = 0;
	} else {
		picIn->i_type = X264_TYPE_AUTO;
	}

	x264_nal_t* nals = nullptr;
	int i_nals = 0;
	x264_picture_t picOut;
	int frameSize = x264_encoder_encode(encoder, &nals, &i_nals, picIn.get(), &picOut);
	if (frameSize < 0) {
		return false;
	}

	// Pack NALs into one Annex B access unit
	outAu.bytes.clear();
	outAu.bytes.reserve(static_cast<size_t>(frameSize));
	for (int i = 0; i < i_nals; ++i) {
		const uint8_t* p = nals[i].p_payload;
		int len = nals[i].i_payload;
		outAu.bytes.insert(outAu.bytes.end(), p, p + len);
	}
	outAu.isKeyframe = (picOut.b_keyframe != 0);
	outAu.timestamp = timestampMs;
	return frameSize > 0;
}

void X264Encoder::requestIDR() {
	forceIdr = 1;
}
