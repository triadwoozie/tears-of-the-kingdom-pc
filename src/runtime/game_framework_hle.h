#pragma once

#include <cstdint>

namespace totk::runtime {

// Registers HLE hooks for GameFrameworkNx main loop progression:
// monotonic system tick timers, vsync event signaling, applet focus,
// and non-blocking system event waiters.
void register_game_framework_hooks();

// Returns true if game framework hooks have been registered
bool is_game_framework_hooks_registered();

} // namespace totk::runtime
