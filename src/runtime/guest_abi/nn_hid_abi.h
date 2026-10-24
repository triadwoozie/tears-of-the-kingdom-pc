#pragma once

#include "guest_types.h"

namespace nn::hid {

enum NpadButton : uint64_t {
    A             = 1ull << 0,
    B             = 1ull << 1,
    X             = 1ull << 2,
    Y             = 1ull << 3,
    StickL        = 1ull << 4,
    StickR        = 1ull << 5,
    L             = 1ull << 6,
    R             = 1ull << 7,
    ZL            = 1ull << 8,
    ZR            = 1ull << 9,
    Plus          = 1ull << 10,
    Minus         = 1ull << 11,
    DLeft         = 1ull << 12,
    DUp           = 1ull << 13,
    DRight        = 1ull << 14,
    DDown         = 1ull << 15,
};

struct AnalogStickState {
    int32_t x;
    int32_t y;
};
TOTK_STATIC_ASSERT_ABI(AnalogStickState, 8);

struct NpadBaseState {
    uint64_t sampling_number;
    uint64_t buttons;
    AnalogStickState analog_stick_l;
    AnalogStickState analog_stick_r;
    uint32_t attributes;
    uint32_t reserved;
};
TOTK_STATIC_ASSERT_ABI(NpadBaseState, 40);
TOTK_STATIC_ASSERT_OFFSETOF(NpadBaseState, sampling_number, 0x00);
TOTK_STATIC_ASSERT_OFFSETOF(NpadBaseState, buttons, 0x08);
TOTK_STATIC_ASSERT_OFFSETOF(NpadBaseState, analog_stick_l, 0x10);
TOTK_STATIC_ASSERT_OFFSETOF(NpadBaseState, analog_stick_r, 0x18);
TOTK_STATIC_ASSERT_OFFSETOF(NpadBaseState, attributes, 0x20);

struct NpadFullKeyState : NpadBaseState {};
TOTK_STATIC_ASSERT_ABI(NpadFullKeyState, 40);

struct NpadHandheldState : NpadBaseState {};
TOTK_STATIC_ASSERT_ABI(NpadHandheldState, 40);

struct NpadJoyDualState : NpadBaseState {};
TOTK_STATIC_ASSERT_ABI(NpadJoyDualState, 40);

struct DirectionState {
    float mtx[3][3];
};
TOTK_STATIC_ASSERT_ABI(DirectionState, 36);

struct SixAxisSensorState {
    uint64_t delta_time;
    uint64_t sampling_number;
    float acceleration[3];
    float angular_velocity[3];
    float angle[3];
    DirectionState direction;
    uint32_t attributes;
    uint32_t reserved;
};
TOTK_STATIC_ASSERT_ABI(SixAxisSensorState, 96);
TOTK_STATIC_ASSERT_OFFSETOF(SixAxisSensorState, delta_time, 0x00);
TOTK_STATIC_ASSERT_OFFSETOF(SixAxisSensorState, sampling_number, 0x08);
TOTK_STATIC_ASSERT_OFFSETOF(SixAxisSensorState, acceleration, 0x10);
TOTK_STATIC_ASSERT_OFFSETOF(SixAxisSensorState, angular_velocity, 0x1C);
TOTK_STATIC_ASSERT_OFFSETOF(SixAxisSensorState, angle, 0x28);
TOTK_STATIC_ASSERT_OFFSETOF(SixAxisSensorState, direction, 0x34);

struct NpadRingLifetime {
    uint64_t current_sampling_number;
    uint32_t entry_count;
    uint32_t reserved;
};
TOTK_STATIC_ASSERT_ABI(NpadRingLifetime, 16);

} // namespace nn::hid
