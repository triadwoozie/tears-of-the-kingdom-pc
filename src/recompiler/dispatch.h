#pragma once

#include "arm64_ctx.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*RecompiledFunc)(Arm64Context *ctx);

// Register a recompiled C function for a given guest virtual address
void register_recompiled_func(uint64_t guest_addr, RecompiledFunc fn);
RecompiledFunc lookup_recompiled_func(uint64_t guest_addr);

// Fast indirect branch & call resolver (used by BLR and dynamic BR)
void guest_indirect_call(Arm64Context *ctx, uint64_t target_addr);

// Radix table initialization
void init_dispatch_table(void);

// Telemetry & crash dump for CPU context
void dump_cpu_state(const Arm64Context *ctx, uint64_t target_addr);

// Symbol table loader & resolver for backtraces
void load_symbols_tsv(const char *path);
const char *lookup_symbol_name(uint64_t addr);

// Dynamic dispatch & jump table resolution for recompiled C modules
void rt_dispatch(Arm64Context *c);
uint64_t resolve_v143_jump_table(uint64_t anchor_addr, int32_t index);
void dispatch_blr_x18(Arm64Context *ctx);
Arm64Context *get_active_guest_context(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace totk::recompiler {
    using ::RecompiledFunc;
    using ::register_recompiled_func;
    using ::lookup_recompiled_func;
    using ::guest_indirect_call;
    using ::rt_dispatch;
    using ::resolve_v143_jump_table;
    using ::dispatch_blr_x18;
    using ::init_dispatch_table;
    using ::dump_cpu_state;
    using ::load_symbols_tsv;
    using ::lookup_symbol_name;
    using ::get_active_guest_context;
}
#endif
