#pragma once

#include <cstdint>

namespace totk::runtime {

// Registers HLE hooks for GameFrameworkNx main loop progression:
// monotonic system tick timers, vsync event signaling, applet focus,
// and non-blocking system event waiters.
void register_game_framework_hooks();

// Returns true if game framework hooks have been registered
bool is_game_framework_hooks_registered();

// Initializes the guest-visible display synchronization event and signals it.
bool initialize_display_vsync_event(uint64_t guest_address);
void signal_display_vsync_event();

} // namespace totk::runtime
