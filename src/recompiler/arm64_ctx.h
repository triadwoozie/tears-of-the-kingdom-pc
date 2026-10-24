#pragma once

#include <cstdint>
#include <simde/arm/neon.h>

namespace totk::recompiler {

#pragma pack(push, 16)
struct alignas(16) Arm64Context {
    // General purpose registers X0-X30 (X31 is zero/ZR or SP depending on encoding)
    uint64_t x[31];
    uint64_t sp;
    uint64_t pc;
    uint32_t nzcv;
    uint32_t fpcr;
    uint32_t fpsr;

    // Exclusive monitor state (LDXR / STXR emulation)
    uint64_t exclusive_addr;
    uint64_t exclusive_val;

    // Horizon OS thread-local storage base (TPIDRRO_EL0)
    uint64_t tpidrro_el0;

    // 32 128-bit FP/Advanced SIMD registers (V0 - V31)
    union {
        __uint128_t u128[32];
        uint64_t u64[32][2];
        uint32_t u32[32][4];
        float f32[32][4];
        double f64[32][2];
        simde_float32x4_t neon_f32[32];
        simde_uint32x4_t neon_u32[32];
    } v;
};
#pragma pack(pop)

// Context initialization helper
inline void init_arm64_context(Arm64Context *ctx, uint64_t initial_sp, uint64_t tls_addr) {
    for (int i = 0; i < 31; i++) ctx->x[i] = 0;
    ctx->sp = initial_sp;
    ctx->pc = 0;
    ctx->nzcv = 0;
    ctx->fpcr = 0;
    ctx->fpsr = 0;
    ctx->exclusive_addr = ~0ull;
    ctx->exclusive_val = 0;
    ctx->tpidrro_el0 = tls_addr;
    for (int i = 0; i < 32; i++) ctx->v.u128[i] = 0;
}

} // namespace totk::recompiler
