#pragma once

#include "guest_abi/nn_hid_abi.h"
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace totk::runtime {

struct HidSharedMemory {
    // 256 KiB Switch nn::hid shared memory page
    // 0x00000: Debug Pad
    // 0x00400: TouchScreen
    // 0x03000: Mouse
    // 0x03400: Keyboard
    // 0x03800: BasicGesture
    // 0x03C00: Digitizer
    // 0x04000: Sleep
    // 0x04800: Npad Controllers 0-9 (each 0x5000 bytes)
    // 0x3A000: Palma
    uint8_t raw[0x40000];
};

class HidHLE {
public:
    static HidHLE &instance();

    void initialize();
    void reset();

    // SDL3 Event Handling
    void update_from_keyboard(const bool *keys, size_t count);
    void update_from_controller(uint64_t buttons, float lx, float ly, float rx, float ry);
    void update_touch(bool touching, float x, float y);

    // Guest Npad State Queries (matching SDK ABI)
    int get_npad_states(nn::hid::NpadFullKeyState *out, int count, uint32_t npad_id);
    int get_npad_states(nn::hid::NpadHandheldState *out, int count, uint32_t npad_id);
    int get_npad_states(nn::hid::NpadJoyDualState *out, int count, uint32_t npad_id);

    // Shared Memory Handle
    uint32_t get_shared_memory_handle() const { return shared_mem_handle_; }
    const uint8_t *get_shared_memory_bytes() const { return shared_memory_.raw; }
    size_t get_shared_memory_size() const { return sizeof(shared_memory_.raw); }

    // Controller configuration
    void set_supported_styles(uint32_t styles) { supported_styles_ = styles; }
    uint32_t get_npad_style_set(uint32_t npad_id) const;

    uint64_t get_sampling_number() const { return sampling_number_; }
    uint64_t get_current_buttons() const { return current_buttons_; }

private:
    HidHLE();
    ~HidHLE() = default;

    mutable std::mutex mutex_;
    uint64_t sampling_number_ = 0;
    uint64_t current_buttons_ = 0;
    int32_t stick_lx_ = 0;
    int32_t stick_ly_ = 0;
    int32_t stick_rx_ = 0;
    int32_t stick_ry_ = 0;

    uint32_t supported_styles_ = 1; // FullKey
    bool player_connected_ = true;
    uint32_t shared_mem_handle_ = 0x10080;

    HidSharedMemory shared_memory_{};
};

} // namespace totk::runtime
