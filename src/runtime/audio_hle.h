#pragma once

#include <SDL3/SDL.h>
#include <atomic>
#include <cstddef>
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

struct AudioRendererConfig {
    uint32_t sample_rate = 48000;
    uint32_t buffer_frame_count = 240;
    uint32_t channel_count = 2;
    uint32_t voice_count = 256;
    uint32_t sink_count = 1;
    uint32_t submix_count = 16;
    uint32_t revision_magic = 0x3f564552; // 'REV?'
};

class AudioRendererHLE {
public:
    static AudioRendererHLE &instance();

    bool initialize(uint32_t sample_rate = 48000, uint32_t channels = 2);
    void shutdown();

    void set_master_volume(float vol) { master_volume_ = vol; }
    float get_master_volume() const { return master_volume_; }

    // Renderer lifecycle & configuration (audren:u / AudioRenderer)
    void open_renderer(const void *config_data, size_t size);
    void start_renderer();
    void stop_renderer();
    bool is_started() const { return started_.load(); }
    uint32_t get_system_event();

    // RequestUpdateAudioRenderer (cmd 4)
    size_t update_audio_renderer(const void *input, size_t in_len, void *output, size_t out_cap);

    // Voice control
    int allocate_voice();
    void set_voice_buffer(int voice_idx, const float *samples, size_t count);
    void start_voice(int voice_idx);
    void stop_voice(int voice_idx);

    // Mix active voices into output buffer and push to SDL3 stream
    void mix_and_submit(size_t frame_count);

    size_t get_active_voice_count() const;
    uint64_t get_rendered_frames() const { return rendered_frames_.load(); }
    uint64_t get_update_call_count() const { return update_call_count_.load(); }

private:
    AudioRendererHLE();
    ~AudioRendererHLE();

    AudioRendererConfig config_{};
    std::atomic<bool> started_{false};
    std::atomic<uint64_t> rendered_frames_{0};
    std::atomic<uint64_t> update_call_count_{0};
    uint32_t system_event_handle_ = 0;

    uint32_t sample_rate_ = 48000;
    uint32_t channels_ = 2;
    float master_volume_ = 1.0f;
    SDL_AudioStream *audio_stream_ = nullptr;
    std::vector<VoiceChannel> voices_;
    std::vector<float> mix_buffer_;
};

} // namespace totk::runtime
