#pragma once

#include <SDL3/SDL.h>
#include <cstdint>
#include <vector>

namespace totk::runtime {

struct VoiceChannel {
    bool is_active = false;
    float volume = 1.0f;
    float pitch = 1.0f;
    uint32_t sample_rate = 48000;
    std::vector<float> sample_buffer;
    size_t play_cursor = 0;
};

class AudioRendererHLE {
public:
    static AudioRendererHLE &instance();

    bool initialize(uint32_t sample_rate = 48000, uint32_t channels = 2);
    void shutdown();

    void set_master_volume(float vol) { master_volume_ = vol; }
    float get_master_volume() const { return master_volume_; }

    int allocate_voice();
    void set_voice_buffer(int voice_idx, const float *samples, size_t count);
    void start_voice(int voice_idx);
    void stop_voice(int voice_idx);

    // Mix active voices into output buffer and push to SDL3 stream
    void mix_and_submit(size_t frame_count);

    size_t get_active_voice_count() const;

private:
    AudioRendererHLE();
    ~AudioRendererHLE();

    uint32_t sample_rate_ = 48000;
    uint32_t channels_ = 2;
    float master_volume_ = 1.0f;
    SDL_AudioStream *audio_stream_ = nullptr;
    std::vector<VoiceChannel> voices_;
    std::vector<float> mix_buffer_;
};

} // namespace totk::runtime
