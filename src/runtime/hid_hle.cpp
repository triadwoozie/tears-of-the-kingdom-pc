#include "hid_hle.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace totk::runtime {

HidHLE &HidHLE::instance() {
    static HidHLE s_instance;
    return s_instance;
}

HidHLE::HidHLE() {
    initialize();
}

void HidHLE::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    reset();
}

void HidHLE::reset() {
    sampling_number_ = 0;
    current_buttons_ = 0;
    stick_lx_ = 0;
    stick_ly_ = 0;
    stick_rx_ = 0;
    stick_ry_ = 0;
    supported_styles_ = 1;
    player_connected_ = true;
    std::memset(shared_memory_.raw, 0, sizeof(shared_memory_.raw));
}

void HidHLE::update_from_controller(uint64_t buttons, float lx, float ly, float rx, float ry) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_buttons_ = buttons;

    auto quantize_stick = [](float val) -> int32_t {
        if (!std::isfinite(val) || std::fabs(val) < 0.10f) return 0;
        float clamped = std::clamp(val, -1.0f, 1.0f);
        return static_cast<int32_t>(std::lround(clamped * 32767.0f));
    };

    stick_lx_ = quantize_stick(lx);
    stick_ly_ = quantize_stick(ly);
    stick_rx_ = quantize_stick(rx);
    stick_ry_ = quantize_stick(ry);
    sampling_number_++;

    // Write to Controller 0 FullKey state in shared memory
    // Offset for Controller 0: 0x4800
    // FullKey ring header: 0x4800
    size_t npad0_offset = 0x4800;
    if (npad0_offset + sizeof(nn::hid::NpadBaseState) <= sizeof(shared_memory_.raw)) {
        nn::hid::NpadBaseState state{};
        state.sampling_number = sampling_number_;
        state.buttons = current_buttons_;
        state.analog_stick_l.x = stick_lx_;
        state.analog_stick_l.y = stick_ly_;
        state.analog_stick_r.x = stick_rx_;
        state.analog_stick_r.y = stick_ry_;
        state.attributes = 3; // Connected + Wired

        std::memcpy(shared_memory_.raw + npad0_offset, &state, sizeof(state));
    }
}

void HidHLE::update_from_keyboard(const bool *keys, size_t count) {
    if (!keys || count == 0) return;
    std::lock_guard<std::mutex> lock(mutex_);

    // Key mapping for desktop WASD / Arrow keys
    // W: DUp / LStick Up, S: DDown / LStick Down, A: DLeft / LStick Left, D: DRight / LStick Right
    // Space: B (Jump), Enter: A (Action), Esc: Plus (Pause)
    uint64_t buttons = 0;
    int32_t lx = 0, ly = 0;

    // Standard SDL scancodes (represented as indices)
    if (count > 26 && keys[26]) { buttons |= nn::hid::NpadButton::DUp; ly += 32767; }    // W
    if (count > 22 && keys[22]) { buttons |= nn::hid::NpadButton::DDown; ly -= 32767; }  // S
    if (count > 4 && keys[4])   { buttons |= nn::hid::NpadButton::DLeft; lx -= 32767; }  // A
    if (count > 7 && keys[7])   { buttons |= nn::hid::NpadButton::DRight; lx += 32767; } // D
    if (count > 44 && keys[44]) { buttons |= nn::hid::NpadButton::B; }                    // Space
    if (count > 40 && keys[40]) { buttons |= nn::hid::NpadButton::A; }                    // Enter
    if (count > 41 && keys[41]) { buttons |= nn::hid::NpadButton::Plus; }                 // Esc

    current_buttons_ = buttons;
    stick_lx_ = lx;
    stick_ly_ = ly;
    sampling_number_++;

    size_t npad0_offset = 0x4800;
    if (npad0_offset + sizeof(nn::hid::NpadBaseState) <= sizeof(shared_memory_.raw)) {
        nn::hid::NpadBaseState state{};
        state.sampling_number = sampling_number_;
        state.buttons = current_buttons_;
        state.analog_stick_l.x = stick_lx_;
        state.analog_stick_l.y = stick_ly_;
        state.analog_stick_r.x = stick_rx_;
        state.analog_stick_r.y = stick_ry_;
        state.attributes = 3;

        std::memcpy(shared_memory_.raw + npad0_offset, &state, sizeof(state));
    }
}

void HidHLE::update_touch(bool touching, float x, float y) {
    std::lock_guard<std::mutex> lock(mutex_);
    // TouchScreen shared memory area is at offset 0x400
    size_t touch_offset = 0x400;
    if (touch_offset + 32 <= sizeof(shared_memory_.raw)) {
        uint64_t touch_sampling = ++sampling_number_;
        uint32_t touch_count = touching ? 1 : 0;
        uint32_t px = static_cast<uint32_t>(std::clamp(x, 0.0f, 1.0f) * 1280.0f);
        uint32_t py = static_cast<uint32_t>(std::clamp(y, 0.0f, 1.0f) * 720.0f);

        std::memcpy(shared_memory_.raw + touch_offset, &touch_sampling, sizeof(touch_sampling));
        std::memcpy(shared_memory_.raw + touch_offset + 8, &touch_count, sizeof(touch_count));
        std::memcpy(shared_memory_.raw + touch_offset + 16, &px, sizeof(px));
        std::memcpy(shared_memory_.raw + touch_offset + 20, &py, sizeof(py));
    }
}

int HidHLE::get_npad_states(nn::hid::NpadFullKeyState *out, int count, uint32_t npad_id) {
    if (!out || count <= 0) return 0;
    std::lock_guard<std::mutex> lock(mutex_);

    for (int i = 0; i < count; ++i) {
        out[i].sampling_number = sampling_number_;
        if (npad_id == 0 && player_connected_) {
            out[i].buttons = current_buttons_;
            out[i].analog_stick_l.x = stick_lx_;
            out[i].analog_stick_l.y = stick_ly_;
            out[i].analog_stick_r.x = stick_rx_;
            out[i].analog_stick_r.y = stick_ry_;
            out[i].attributes = 3;
        } else {
            out[i].buttons = 0;
            out[i].analog_stick_l = {0, 0};
            out[i].analog_stick_r = {0, 0};
            out[i].attributes = 0;
        }
    }
    return count;
}

int HidHLE::get_npad_states(nn::hid::NpadHandheldState *out, int count, uint32_t npad_id) {
    return get_npad_states(reinterpret_cast<nn::hid::NpadFullKeyState *>(out), count, npad_id);
}

int HidHLE::get_npad_states(nn::hid::NpadJoyDualState *out, int count, uint32_t npad_id) {
    return get_npad_states(reinterpret_cast<nn::hid::NpadFullKeyState *>(out), count, npad_id);
}

uint32_t HidHLE::get_npad_style_set(uint32_t npad_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (npad_id == 0 && player_connected_) {
        return supported_styles_;
    }
    return 0;
}

} // namespace totk::runtime
