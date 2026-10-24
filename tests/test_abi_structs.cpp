#include "guest_abi/guest_types.h"
#include "guest_abi/nn_hid_abi.h"
#include "guest_abi/sead_framework_abi.h"
#include <cassert>
#include <cstdio>

int main() {
    std::printf("[Test] Testing Guest ABI Struct Layouts...\n");

    // nn::hid structs
    assert(sizeof(nn::hid::AnalogStickState) == 8);
    assert(sizeof(nn::hid::NpadBaseState) == 40);
    assert(sizeof(nn::hid::NpadFullKeyState) == 40);
    assert(sizeof(nn::hid::NpadHandheldState) == 40);
    assert(sizeof(nn::hid::NpadJoyDualState) == 40);
    assert(sizeof(nn::hid::DirectionState) == 36);
    assert(sizeof(nn::hid::SixAxisSensorState) == 96);
    assert(sizeof(nn::hid::NpadRingLifetime) == 16);

    // Offsets
    assert(offsetof(nn::hid::NpadBaseState, sampling_number) == 0x00);
    assert(offsetof(nn::hid::NpadBaseState, buttons) == 0x08);
    assert(offsetof(nn::hid::NpadBaseState, analog_stick_l) == 0x10);
    assert(offsetof(nn::hid::NpadBaseState, analog_stick_r) == 0x18);
    assert(offsetof(nn::hid::NpadBaseState, attributes) == 0x20);

    assert(offsetof(nn::hid::SixAxisSensorState, delta_time) == 0x00);
    assert(offsetof(nn::hid::SixAxisSensorState, sampling_number) == 0x08);
    assert(offsetof(nn::hid::SixAxisSensorState, acceleration) == 0x10);
    assert(offsetof(nn::hid::SixAxisSensorState, angular_velocity) == 0x1C);
    assert(offsetof(nn::hid::SixAxisSensorState, angle) == 0x28);
    assert(offsetof(nn::hid::SixAxisSensorState, direction) == 0x34);

    // sead structs
    assert(sizeof(sead::Vector3f) == 12);
    assert(sizeof(sead::Matrix34f) == 48);
    assert(sizeof(sead::SafeStringBase<char>) == 8);
    assert(sizeof(sead::Heap) == 64);
    assert(sizeof(sead::GameTimingState) == 16);
    assert(sizeof(sead::RenderScaleState) == 16);

    assert(offsetof(sead::Heap, vtable) == 0x00);
    assert(offsetof(sead::Heap, name) == 0x08);
    assert(offsetof(sead::Heap, parent) == 0x10);
    assert(offsetof(sead::Heap, child_list) == 0x18);
    assert(offsetof(sead::Heap, size) == 0x28);
    assert(offsetof(sead::Heap, start_address) == 0x30);

    std::printf("[Test] All ABI struct sizes and offsets verified successfully!\n");
    return 0;
}
