#include "audio_hle.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace totk::runtime {

AudioRendererHLE &AudioRendererHLE::instance() {
    static AudioRendererHLE a;
    return a;
}

AudioRendererHLE::AudioRendererHLE() {
    system_event_handle_ = 0x20001; // Mock handle for audio vsync/timer event
}

AudioRendererHLE::~AudioRendererHLE() {
    shutdown();
}

bool AudioRendererHLE::initialize(uint32_t sample_rate, uint32_t channels) {
    sample_rate_ = sample_rate;
    channels_ = channels;
    voices_.resize(256); // 256 concurrent Horizon audio voices

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = static_cast<int>(channels_);
    spec.freq = static_cast<int>(sample_rate_);

    audio_stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!audio_stream_) {
        // Headless audio fallback mode
        std::printf("[AudioRenderer] Running in headless audio fallback mode: %s\n", SDL_GetError());
        return true;
    }

    SDL_ResumeAudioStreamDevice(audio_stream_);
    std::printf("[AudioRenderer] Initialized 48 kHz %u-channel SDL3 Audio Stream\n", channels_);
    return true;
}

void AudioRendererHLE::shutdown() {
    if (audio_stream_) {
        SDL_DestroyAudioStream(audio_stream_);
        audio_stream_ = nullptr;
    }
    started_.store(false);
}

void AudioRendererHLE::open_renderer(const void *config_data, size_t size) {
    if (config_data && size >= sizeof(uint32_t) * 13) {
        const uint32_t *u32 = reinterpret_cast<const uint32_t *>(config_data);
        config_.sample_rate = u32[0];
        config_.buffer_frame_count = u32[1];
        config_.channel_count = u32[2];
        config_.voice_count = u32[4];
        config_.sink_count = u32[5];
        config_.submix_count = u32[6];
        config_.revision_magic = u32[12];
    }
}

void AudioRendererHLE::start_renderer() {
    started_.store(true);
}

void AudioRendererHLE::stop_renderer() {
    started_.store(false);
}

uint32_t AudioRendererHLE::get_system_event() {
    return system_event_handle_;
}

size_t AudioRendererHLE::update_audio_renderer(const void *input, size_t in_len, void *output, size_t out_cap) {
    update_call_count_++;
    if (!output || out_cap < 64) return 0;

    uint8_t *out_bytes = reinterpret_cast<uint8_t *>(output);

    // If input is too small, synthesize a valid minimal 64-byte response
    if (!input || in_len < 64) {
        uint32_t minimal_hdr[16]{0};
        minimal_hdr[0] = config_.revision_magic ? config_.revision_magic : 0x3f564552;
        minimal_hdr[15] = 64;
        std::memcpy(out_bytes, minimal_hdr, sizeof(minimal_hdr));
        return 64;
    }

    uint32_t in_hdr[16]{0};
    std::memcpy(in_hdr, input, sizeof(in_hdr));

    uint32_t magic = in_hdr[0]; // Echo client revision magic
    unsigned pools = config_.voice_count * 4 + config_.submix_count;
    if (pools == 0) pools = 256;

    uint32_t out_hdr[16]{0};
    out_hdr[0] = magic;
    out_hdr[1] = 0xb0;
    out_hdr[2] = pools * 16;
    out_hdr[3] = config_.voice_count * 16;
    out_hdr[4] = 0;
    out_hdr[5] = config_.submix_count * 0x90;
    out_hdr[6] = 0;
    out_hdr[7] = config_.sink_count * 32;
    out_hdr[8] = 16;
    out_hdr[9] = 0;
    out_hdr[10] = 16;

    size_t required_output_size = 64;
    for (unsigned i = 1; i < 15; ++i) {
        required_output_size += out_hdr[i];
    }

    size_t actual_out_size = std::min(required_output_size, out_cap);
    out_hdr[15] = static_cast<uint32_t>(actual_out_size);

    std::memset(out_bytes, 0, actual_out_size);
    std::memcpy(out_bytes, out_hdr, sizeof(out_hdr));

    // Fill pool states so client sees initialized/active memory pools
    const uint8_t *in_bytes = reinterpret_cast<const uint8_t *>(input);
    if (in_len >= 64 + in_hdr[1]) {
        for (unsigned i = 0; i < pools && (64 + (i + 1) * 16 <= actual_out_size); ++i) {
            uint32_t state = 0;
            if (64 + in_hdr[1] + (i + 1) * 32 <= in_len) {
                std::memcpy(&state, in_bytes + 64 + in_hdr[1] + i * 32 + 16, sizeof(state));
            }
            // State conversion: 4 -> 5 (Attached), 2 -> 3 (RequestAttach)
            state = (state == 4) ? 5 : (state == 2) ? 3 : 0;
            std::memcpy(out_bytes + 64 + i * 16, &state, sizeof(state));
        }
    }

    // Set frame counter at output tail
    uint64_t current_frame = rendered_frames_.fetch_add(1);
    if (actual_out_size >= 16) {
        std::memcpy(out_bytes + actual_out_size - 16, &current_frame, sizeof(current_frame));
    }

    return actual_out_size;
}

int AudioRendererHLE::allocate_voice() {
    for (size_t i = 0; i < voices_.size(); i++) {
        if (!voices_[i].is_active) {
            voices_[i].is_active = true;
            voices_[i].play_cursor = 0;
            return static_cast<int>(i);
        }
    }
    return -1;
}

void AudioRendererHLE::set_voice_buffer(int voice_idx, const float *samples, size_t count) {
    if (voice_idx < 0 || voice_idx >= static_cast<int>(voices_.size())) return;
    auto &v = voices_[voice_idx];
    v.sample_buffer.assign(samples, samples + count);
    v.play_cursor = 0;
}

void AudioRendererHLE::start_voice(int voice_idx) {
    if (voice_idx >= 0 && voice_idx < static_cast<int>(voices_.size())) {
        voices_[voice_idx].is_active = true;
    }
}

void AudioRendererHLE::stop_voice(int voice_idx) {
    if (voice_idx >= 0 && voice_idx < static_cast<int>(voices_.size())) {
        voices_[voice_idx].is_active = false;
        voices_[voice_idx].sample_buffer.clear();
    }
}

void AudioRendererHLE::mix_and_submit(size_t frame_count) {
    size_t sample_count = frame_count * channels_;
    if (mix_buffer_.size() < sample_count) {
        mix_buffer_.resize(sample_count);
    }
    std::fill(mix_buffer_.begin(), mix_buffer_.begin() + sample_count, 0.0f);

    for (auto &v : voices_) {
        if (!v.is_active || v.sample_buffer.empty()) continue;

        for (size_t f = 0; f < frame_count; f++) {
            if (v.play_cursor < v.sample_buffer.size()) {
                float sample = v.sample_buffer[v.play_cursor++] * v.volume * master_volume_;
                for (uint32_t c = 0; c < channels_; c++) {
                    mix_buffer_[f * channels_ + c] += sample;
                }
            } else {
                v.is_active = false;
                break;
            }
        }
    }

    // Clamp mixed samples to [-1.0, 1.0]
    for (size_t i = 0; i < sample_count; i++) {
        mix_buffer_[i] = std::clamp(mix_buffer_[i], -1.0f, 1.0f);
    }

    if (audio_stream_) {
        SDL_PutAudioStreamData(audio_stream_, mix_buffer_.data(), static_cast<int>(sample_count * sizeof(float)));
    }
}

size_t AudioRendererHLE::get_active_voice_count() const {
    size_t count = 0;
    for (const auto &v : voices_) {
        if (v.is_active) count++;
    }
    return count;
}

} // namespace totk::runtime
