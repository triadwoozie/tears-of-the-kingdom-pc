#include "game_framework_hle.h"
#include "recompiler/dispatch.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>

extern "C" uint64_t rt_ticks(void);

namespace totk::runtime {

static std::atomic<bool> s_hooks_registered{false};

// Synthetic non-blocking SystemEventType instance for VI vsync and notification events
struct alignas(8) HleSystemEventType {
    uint32_t state = 1;               // 1 = Initialized
    uint8_t auto_clear = 1;           // 1 = AutoClear
    uint8_t readable_managed = 1;
    uint8_t writable_managed = 1;
    uint8_t pad = 0;
    uint32_t readable_handle = 0x20001;
    uint32_t writable_handle = 0x20002;
    uint64_t internal_ptr = 0;
    uint64_t user_data = 0;
    uint8_t reserved[16]{};
};

[[maybe_unused]] static HleSystemEventType s_vsync_event;
static HleSystemEventType s_notification_event;

void register_game_framework_hooks() {
    if (s_hooks_registered.exchange(true)) {
        return;
    }

    std::printf("[HLE] Registering GameFrameworkNx & Horizon loop progression hooks...\n");

    // 1. Monotonic System Tick SDK entrypoints (19.2 MHz Horizon hardware clock)
    // nn::os::GetSystemTick (0x710c140e10)
    totk::recompiler::register_recompiled_func(0x710c140e10ULL, [](Arm64Context *c) {
        c->x[0] = rt_ticks();
        c->pc = c->x[30];
    });

    // nn::os::GetSystemTickOrdered (0x710c140e20)
    totk::recompiler::register_recompiled_func(0x710c140e20ULL, [](Arm64Context *c) {
        c->x[0] = rt_ticks();
        c->pc = c->x[30];
    });

    // nn::os::GetSystemTickFrequency (0x710c140e40)
    totk::recompiler::register_recompiled_func(0x710c140e40ULL, [](Arm64Context *c) {
        c->x[0] = 19200000ULL; // 19.2 MHz
        c->pc = c->x[30];
    });

    // nnosGetSystemTick (0x710c141630)
    totk::recompiler::register_recompiled_func(0x710c141630ULL, [](Arm64Context *c) {
        c->x[0] = rt_ticks();
        c->pc = c->x[30];
    });

    // nnosGetSystemTickOrdered (0x710c141640)
    totk::recompiler::register_recompiled_func(0x710c141640ULL, [](Arm64Context *c) {
        c->x[0] = rt_ticks();
        c->pc = c->x[30];
    });

    // nnosGetSystemTickFrequency (0x710c141650)
    totk::recompiler::register_recompiled_func(0x710c141650ULL, [](Arm64Context *c) {
        c->x[0] = 19200000ULL;
        c->pc = c->x[30];
    });

    // 2. Non-blocking SystemEvent waits so the render / main thread never starves
    // nn::os::WaitSystemEvent (0x710c13ce00)
    totk::recompiler::register_recompiled_func(0x710c13ce00ULL, [](Arm64Context *c) {
        c->x[0] = 0; // ResultSuccess
        c->pc = c->x[30];
    });

    // nnosWaitSystemEvent (0x710c141310)
    totk::recompiler::register_recompiled_func(0x710c141310ULL, [](Arm64Context *c) {
        c->x[0] = 0; // ResultSuccess
        c->pc = c->x[30];
    });

    // nn::os::TryWaitSystemEvent (0x710c13ce30)
    totk::recompiler::register_recompiled_func(0x710c13ce30ULL, [](Arm64Context *c) {
        c->x[0] = 1; // true = signaled
        c->pc = c->x[30];
    });

    // nnosTryWaitSystemEvent (0x710c141320)
    totk::recompiler::register_recompiled_func(0x710c141320ULL, [](Arm64Context *c) {
        c->x[0] = 1; // true = signaled
        c->pc = c->x[30];
    });

    // nn::os::TimedWaitSystemEvent (0x710c13ce60)
    totk::recompiler::register_recompiled_func(0x710c13ce60ULL, [](Arm64Context *c) {
        c->x[0] = 0; // ResultSuccess
        c->pc = c->x[30];
    });

    // nnosTimedWaitSystemEvent (0x710c141330)
    totk::recompiler::register_recompiled_func(0x710c141330ULL, [](Arm64Context *c) {
        c->x[0] = 0; // ResultSuccess
        c->pc = c->x[30];
    });

    // 3. Notification Message Event
    // nn::oe::GetNotificationMessageEvent (0x710c4084c0)
    totk::recompiler::register_recompiled_func(0x710c4084c0ULL, [](Arm64Context *c) {
        c->x[0] = reinterpret_cast<uint64_t>(&s_notification_event);
        c->pc = c->x[30];
    });

    // 4. Attach/Detach readable handle helpers
    // nn::os::AttachReadableHandleToSystemEvent (0x710c13cd30)
    totk::recompiler::register_recompiled_func(0x710c13cd30ULL, [](Arm64Context *c) {
        auto *event = reinterpret_cast<HleSystemEventType *>(c->x[0]);
        uint32_t handle = static_cast<uint32_t>(c->x[1]);
        uint8_t is_managed = static_cast<uint8_t>(c->x[2]);
        uint8_t clear_mode = static_cast<uint8_t>(c->x[3]);
        if (event) {
            event->state = 1;
            event->readable_handle = handle;
            event->readable_managed = is_managed;
            event->auto_clear = clear_mode;
        }
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    std::printf("  [HLE] GameFrameworkNx & Horizon monotonic progression initialized.\n");
}

bool is_game_framework_hooks_registered() {
    return s_hooks_registered.load();
}

} // namespace totk::runtime
