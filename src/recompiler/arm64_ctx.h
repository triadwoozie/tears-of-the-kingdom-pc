#pragma once

#include "runtime/cpu.h"

#ifdef __cplusplus
namespace totk::recompiler {

using Arm64Context = ::Cpu;

// Context initialization helper
inline void init_arm64_context(Arm64Context *ctx, uint64_t initial_sp, uint64_t tls_addr) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->sp = initial_sp;
    ctx->tpidrro_el0 = tls_addr;
    ctx->exclusive_addr = ~0ull;
}

} // namespace totk::recompiler
#endif
