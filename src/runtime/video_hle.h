#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace totk::runtime {

struct VideoFrame {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 0; // NV12 or RGBA
    std::vector<uint8_t> frame_bytes;
};

class VideoDecoderHLE {
public:
    static VideoDecoderHLE &instance();

    bool initialize();
    bool open_movie(const std::string &path);
    bool decode_next_frame(VideoFrame &out_frame);
    void close_movie();

    bool is_movie_playing() const { return is_playing_; }
    uint32_t get_current_frame() const { return current_frame_; }

private:
    VideoDecoderHLE();
    ~VideoDecoderHLE();

    bool is_playing_ = false;
    uint32_t current_frame_ = 0;
    uint32_t total_frames_ = 0;
    std::string current_movie_path_;
};

} // namespace totk::runtime
