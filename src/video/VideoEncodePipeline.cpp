#include "VideoEncodePipeline.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include "util/Logger.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

VideoEncodePipeline::VideoEncodePipeline() = default;
VideoEncodePipeline::~VideoEncodePipeline() { stop(); }

bool VideoEncodePipeline::start(const FFmpegEncoderConfig& encCfg, const HlsMuxerConfig& muxCfg, HlsFileCallback hlsCb) {
	stop();
	encoderCfg = encCfg;
	muxerCfg = muxCfg;
	fileCallback = std::move(hlsCb);
	pool.clear();
	pool.resize(4);
	head = tail = size = 0;
	running = true;
	worker = std::thread(&VideoEncodePipeline::workerLoop, this);
	return true;
}

void VideoEncodePipeline::stop() {
	if (running.exchange(false)) {
		cv.notify_all();
		if (worker.joinable()) worker.join();
	}
	muxer.shutdown();
	encoder.shutdown();
}

void VideoEncodePipeline::pushI420(const char* y, const char* u, const char* v,
								   unsigned int width, unsigned int height,
								   unsigned long long timestampMs) {
	if (!running) return;
	std::unique_lock<std::mutex> lock(mtx);
	if (size == pool.size()) {
		Util::Logger::getInstance().warn("VideoEncodePipeline queue full: dropping frame");
		return;
	}
	Frame& f = pool[head];
	f.width = width;
	f.height = height;
	f.ts = timestampMs;
	size_t ySize = static_cast<size_t>(width) * height;
	size_t cW = width / 2;
	size_t cH = height / 2;
	size_t cSize = cW * cH;
	f.y.resize(ySize);
	f.u.resize(cSize);
	f.v.resize(cSize);
	std::memcpy(f.y.data(), y, ySize);
	std::memcpy(f.u.data(), u, cSize);
	std::memcpy(f.v.data(), v, cSize);
	f.occupied = true;
	head = (head + 1) % pool.size();
	++size;
	lock.unlock();
	cv.notify_one();
}

void VideoEncodePipeline::requestIDR() {
	encoder.requestIDR();
}

bool VideoEncodePipeline::ensureEncoder(unsigned int w, unsigned int h) {
	if (encoder.getWidth() == static_cast<int>(w) && 
	    encoder.getHeight() == static_cast<int>(h)) {
		return true;
	}

	if (encoder.getWidth() == 0) {
		FFmpegEncoderConfig cfg = encoderCfg;
		cfg.width = static_cast<int>(w);
		cfg.height = static_cast<int>(h);
		
		if (!encoder.initialize(cfg)) {
			return false;
		}

		// Initialize muxer
		HlsMuxerConfig muxCfg = muxerCfg;
		muxCfg.width = static_cast<int>(w);
		muxCfg.height = static_cast<int>(h);
		muxCfg.fps = cfg.fps;
		
		if (!muxer.initialize(muxCfg, fileCallback)) {
			return false;
		}

		// Copy codec parameters from encoder to muxer stream
		AVCodecParameters* codecParams = muxer.getVideoCodecParams();
		AVCodecContext* codecCtx = encoder.getCodecContext();
		if (codecParams && codecCtx) {
			avcodec_parameters_from_context(codecParams, codecCtx);
			codecParams->format = codecCtx->pix_fmt;
		}

		if (!muxer.start()) {
			return false;
		}

		return true;
	}

	// Reinitialize if dimensions changed
	FFmpegEncoderConfig cfg = encoderCfg;
	cfg.width = static_cast<int>(w);
	cfg.height = static_cast<int>(h);
	return encoder.reinitialize(cfg);
}

void VideoEncodePipeline::workerLoop() {
	while (running) {
		Frame f;
		{
			std::unique_lock<std::mutex> lock(mtx);
			cv.wait(lock, [&]{ return !running || size > 0; });
			if (!running) break;
			Frame& slot = pool[tail];
			f = slot; // copy out
			slot = Frame{}; // release memory for reuse
			tail = (tail + 1) % pool.size();
			--size;
		}

		if (!ensureEncoder(f.width, f.height)) {
			Util::Logger::getInstance().error("HLS encoder initialization failed");
			continue;
		}

		// Encode frame (timestamp in microseconds)
		int64_t ptsUs = static_cast<int64_t>(f.ts) * 1000;
		AVPacket* pkt = encoder.encodeI420(f.y.data(), f.u.data(), f.v.data(), 
		                                   f.width, f.height, ptsUs);
		if (!pkt) continue;

		// Write packet to HLS muxer
		bool written = muxer.writePacket(pkt);
		av_packet_unref(pkt);
		
		if (!written) {
			Util::Logger::getInstance().error("Failed to write packet to HLS muxer");
		}
	}

	Util::Logger::getInstance().info("VideoEncodePipeline worker loop exited");
}
