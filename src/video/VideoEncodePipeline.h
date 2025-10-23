#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

#include "FFmpegEncoder.h"
#include "HlsMuxer.h"

class VideoEncodePipeline {
public:
	using HlsFileCallback = std::function<void(const char* filename, const uint8_t* data, size_t size, int is_playlist, uint64_t sequence)>;

	VideoEncodePipeline();
	~VideoEncodePipeline();

	// Start HLS encoding pipeline
	bool start(const FFmpegEncoderConfig& encoderCfg, const HlsMuxerConfig& muxerCfg, HlsFileCallback hlsCb);
	void stop();

	// Non-blocking push; copies planes into internal pool buffer. Drops if overloaded.
	void pushI420(const char* y, const char* u, const char* v,
	             unsigned int width, unsigned int height,
	             unsigned long long timestampMs);

	void requestIDR();

private:
	struct Frame {
		std::vector<uint8_t> y;
		std::vector<uint8_t> u;
		std::vector<uint8_t> v;
		unsigned int width = 0;
		unsigned int height = 0;
		unsigned long long ts = 0;
		bool occupied = false;
	};

	void workerLoop();
	bool ensureEncoder(unsigned int w, unsigned int h);

private:
	std::thread worker;
	std::atomic<bool> running{false};
	
	FFmpegEncoder encoder;
	HlsMuxer muxer;
	FFmpegEncoderConfig encoderCfg;
	HlsMuxerConfig muxerCfg;
	HlsFileCallback fileCallback;
	
	std::mutex mtx;
	std::condition_variable cv;
	std::vector<Frame> pool; // small ring buffer
	size_t head = 0; // write
	size_t tail = 0; // read
	size_t size = 0; // occupied count
};


