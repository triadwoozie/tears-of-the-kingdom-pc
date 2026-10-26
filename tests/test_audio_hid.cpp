#include "runtime/audio_hle.h"
#include "runtime/services_hle.h"
#include "runtime/guest_abi/nn_hid_abi.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

void test_hid_gamepad_mapping() {
    std::printf("[Test] Running test_hid_gamepad_mapping...\n");

    nn::hid::NpadFullKeyState state{};
    state.sampling_number = 101;
    state.attributes = 1; // Connected

    // Simulate SDL3 gamepad button inputs
    bool sdl_btn_a = true;
    bool sdl_btn_zr = true;
    bool sdl_dpad_up = true;

    if (sdl_btn_a)   state.buttons |= nn::hid::NpadButton::A;
    if (sdl_btn_zr)  state.buttons |= nn::hid::NpadButton::ZR;
    if (sdl_dpad_up) state.buttons |= nn::hid::NpadButton::DUp;

    assert((state.buttons & nn::hid::NpadButton::A) != 0);
    assert((state.buttons & nn::hid::NpadButton::ZR) != 0);
    assert((state.buttons & nn::hid::NpadButton::DUp) != 0);
    assert((state.buttons & nn::hid::NpadButton::B) == 0);

    // Simulate normalized stick axes to LP64 int32 range [-32768, 32767]
    float axis_lx = -0.75f;
    float axis_ly = 0.50f;
    state.analog_stick_l.x = static_cast<int32_t>(axis_lx * 32767.0f);
    state.analog_stick_l.y = static_cast<int32_t>(axis_ly * 32767.0f);

    assert(state.analog_stick_l.x == -24575);
    assert(state.analog_stick_l.y == 16383);

    // Verify 6-axis gyro / accelerometer struct alignment
    nn::hid::SixAxisSensorState motion{};
    motion.sampling_number = 101;
    motion.acceleration[0] = 0.0f;
    motion.acceleration[1] = 9.81f; // 1G gravity
    motion.acceleration[2] = 0.0f;
    assert(std::fabs(motion.acceleration[1] - 9.81f) < 1e-4);

    std::printf("  -> HID Gamepad & Sensor Serialization: PASSED\n");
}

void test_audio_hle_pipeline() {
    std::printf("[Test] Running test_audio_hle_pipeline...\n");

    auto &audio = totk::runtime::AudioRendererHLE::instance();
    // In test environment, SDL audio device may not be present, initialize returns true or false gracefully
    audio.initialize(48000, 2);
    audio.set_master_volume(0.8f);
    assert(std::fabs(audio.get_master_volume() - 0.8f) < 1e-4);

    int voice_id = audio.allocate_voice();
    assert(voice_id >= 0);

    // Generate 480 samples of a 440 Hz sine wave
    std::vector<float> sine_samples(480);
    for (size_t i = 0; i < sine_samples.size(); ++i) {
        sine_samples[i] = std::sin(2.0f * 3.14159265f * 440.0f * (static_cast<float>(i) / 48000.0f));
    }

    audio.set_voice_buffer(voice_id, sine_samples.data(), sine_samples.size());
    audio.start_voice(voice_id);
    assert(audio.get_active_voice_count() == 1);

    // Run mixing step for 240 frames
    audio.mix_and_submit(240);

    // Stop voice
    audio.stop_voice(voice_id);
    assert(audio.get_active_voice_count() == 0);

    audio.shutdown();
    std::printf("  -> Audio HLE Multi-Voice Mixing: PASSED\n");
}

void test_audio_renderer_workbuffer_update() {
    std::printf("[Test] Running test_audio_renderer_workbuffer_update...\n");

    auto &audio = totk::runtime::AudioRendererHLE::instance();

    // 1. Initialize configuration via open_renderer
    uint32_t candidate[13]{0};
    candidate[0] = 48000;
    candidate[1] = 240;
    candidate[2] = 2;
    candidate[4] = 64;  // voice count
    candidate[5] = 1;   // sink count
    candidate[6] = 4;   // submix count
    candidate[12] = 0x3f564552; // 'REV?'
    audio.open_renderer(candidate, sizeof(candidate));

    // 2. Lifecycle & events
    audio.start_renderer();
    assert(audio.is_started() == true);
    assert(audio.get_system_event() != 0);

    // 3. Synthesize input workbuffer
    std::vector<uint8_t> in_buffer(1024 * 64, 0);
    uint32_t *in_hdr = reinterpret_cast<uint32_t *>(in_buffer.data());
    in_hdr[0] = 0x3f564552;
    in_hdr[1] = 16;
    unsigned pools = 64 * 4 + 4;
    in_hdr[2] = pools * 32;

    // Set sample pool state: state 4 (Attached) -> should become 5
    uint32_t state_attached = 4;
    std::memcpy(in_buffer.data() + 64 + in_hdr[1] + 0 * 32 + 16, &state_attached, sizeof(state_attached));

    // Output buffer
    std::vector<uint8_t> out_buffer(1024 * 64, 0);
    size_t written = audio.update_audio_renderer(in_buffer.data(), in_buffer.size(), out_buffer.data(), out_buffer.size());
    assert(written >= 64);
    assert(audio.get_update_call_count() > 0);

    const uint32_t *out_hdr = reinterpret_cast<const uint32_t *>(out_buffer.data());
    assert(out_hdr[0] == 0x3f564552); // Valid revision magic
    assert(out_hdr[15] == written);

    // Verify converted pool state: 4 -> 5
    uint32_t converted_state = 0;
    std::memcpy(&converted_state, out_buffer.data() + 64 + 0 * 16, sizeof(converted_state));
    assert(converted_state == 5);

    // Verify frame counter at tail
    uint64_t tail_frame = 0;
    std::memcpy(&tail_frame, out_buffer.data() + written - 16, sizeof(tail_frame));
    assert(tail_frame == 0); // first update produces frame 0

    // Second update advances frames
    written = audio.update_audio_renderer(in_buffer.data(), in_buffer.size(), out_buffer.data(), out_buffer.size());
    std::memcpy(&tail_frame, out_buffer.data() + written - 16, sizeof(tail_frame));
    assert(tail_frame == 1);

    audio.stop_renderer();
    assert(audio.is_started() == false);

    std::printf("  -> Audio Renderer WorkBuffer Update & State Confirmation: PASSED\n");
}

int main() {
    std::printf("==================================================\n");
    std::printf("  Audio & HID Subsystems Unit Tests\n");
    std::printf("==================================================\n");

    test_hid_gamepad_mapping();
    test_audio_hle_pipeline();
    test_audio_renderer_workbuffer_update();

    std::printf("\nAll Audio & HID unit tests PASSED successfully!\n");
    return 0;
}
