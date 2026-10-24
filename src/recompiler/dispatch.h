#pragma once

#include "arm64_ctx.h"
#include <cstdint>

namespace totk::recompiler {

using RecompiledFunc = void (*)(Arm64Context *ctx);

// Register a recompiled C function for a given guest virtual address
void register_recompiled_func(uint64_t guest_addr, RecompiledFunc fn);

// Fast indirect branch & call resolver (used by BLR and dynamic BR)
void guest_indirect_call(Arm64Context *ctx, uint64_t target_addr);

// Resolve TotK v1.4.3 static signed jump tables
uint64_t resolve_v143_jump_table(uint64_t anchor_addr, int32_t index);

// Handle blr x18 system call thunking
void dispatch_blr_x18(Arm64Context *ctx);

// Radix table initialization
void init_dispatch_table();

} // namespace totk::recompiler
