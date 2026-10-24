#pragma once

#include "guest_types.h"

namespace sead {

struct Vector3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};
TOTK_STATIC_ASSERT_ABI(Vector3f, 12);

struct Matrix34f {
    float m[3][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f}
    };
};
TOTK_STATIC_ASSERT_ABI(Matrix34f, 48);

template <typename T>
struct SafeStringBase {
    totk::abi::GuestPtr<const T> string_ptr;
};
TOTK_STATIC_ASSERT_ABI(SafeStringBase<char>, 8);

// sead::Heap guest header layout
struct Heap {
    totk::abi::GuestAddr vtable;
    SafeStringBase<char> name;
    totk::abi::GuestAddr parent;
    totk::abi::GuestAddr child_list[2];
    uint64_t size;
    totk::abi::GuestAddr start_address;
    uint32_t direction;
    uint32_t flags;
};
TOTK_STATIC_ASSERT_ABI(Heap, 64);
TOTK_STATIC_ASSERT_OFFSETOF(Heap, vtable, 0x00);
TOTK_STATIC_ASSERT_OFFSETOF(Heap, name, 0x08);
TOTK_STATIC_ASSERT_OFFSETOF(Heap, parent, 0x10);
TOTK_STATIC_ASSERT_OFFSETOF(Heap, child_list, 0x18);
TOTK_STATIC_ASSERT_OFFSETOF(Heap, size, 0x28);
TOTK_STATIC_ASSERT_OFFSETOF(Heap, start_address, 0x30);

// TotK game timing state (from totk-mobile/tools/game_timing.py)
struct GameTimingState {
    float target_fps;        // 30.0 or 60.0
    float delta_time;        // 1.0 / target_fps
    uint32_t cutscene_flag;
    uint32_t speed_multiplier;
};
TOTK_STATIC_ASSERT_ABI(GameTimingState, 16);

// TotK dynamic render scale / aspect ratio state
struct RenderScaleState {
    float scale_x;
    float scale_y;
    float aspect_ratio;
    float fov_multiplier;
};
TOTK_STATIC_ASSERT_ABI(RenderScaleState, 16);

} // namespace sead
