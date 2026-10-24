#include "audio_hle.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace totk::runtime {

AudioRendererHLE &AudioRendererHLE::instance() {
    static AudioRendererHLE a;
    return a;
}

AudioRendererHLE::AudioRendererHLE() = default;

AudioRendererHLE::~AudioRendererHLE() {
    shutdown();
}

bool AudioRendererHLE::initialize(uint32_t sample_rate, uint32_t channels) {
    sample_rate_ = sample_rate;
    channels_ = channels;
    voices_.resize(32); // 32 concurrent Horizon audio voices

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = static_cast<int>(channels_);
    spec.freq = static_cast<int>(sample_rate_);

    audio_stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!audio_stream_) {
        // Fallback to dummy/headless mixing mode
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
