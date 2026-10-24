#include "video_hle.h"
#include <cstdio>
#include <cstring>

namespace totk::runtime {

VideoDecoderHLE &VideoDecoderHLE::instance() {
    static VideoDecoderHLE v;
    return v;
}

VideoDecoderHLE::VideoDecoderHLE() = default;
VideoDecoderHLE::~VideoDecoderHLE() = default;

bool VideoDecoderHLE::initialize() {
    std::printf("[VideoDecoder] Initialized NVDEC / Cutscene Video Decoding Subsystem\n");
    return true;
}

bool VideoDecoderHLE::open_movie(const std::string &path) {
    current_movie_path_ = path;
    current_frame_ = 0;
    total_frames_ = 900; // Mock 30s cutscene @ 30fps
    is_playing_ = true;
    std::printf("[VideoDecoder] Opened movie file: %s (%u frames)\n", path.c_str(), total_frames_);
    return true;
}

bool VideoDecoderHLE::decode_next_frame(VideoFrame &out_frame) {
    if (!is_playing_ || current_frame_ >= total_frames_) {
        is_playing_ = false;
        return false;
    }

    out_frame.width = 1920;
    out_frame.height = 1080;
    out_frame.format = 1; // RGBA
    out_frame.frame_bytes.resize(1920 * 1080 * 4);

    // Fill with simulated video gradient
    uint32_t *pixels = reinterpret_cast<uint32_t *>(out_frame.frame_bytes.data());
    uint32_t color = 0xFF000000 | ((current_frame_ & 0xFF) << 16) | (((current_frame_ * 2) & 0xFF) << 8);
    for (size_t i = 0; i < 1920 * 1080; i++) {
        pixels[i] = color;
    }

    current_frame_++;
    return true;
}

void VideoDecoderHLE::close_movie() {
    is_playing_ = false;
    current_frame_ = 0;
    current_movie_path_.clear();
}

} // namespace totk::runtime
