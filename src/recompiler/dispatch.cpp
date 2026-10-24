#include "dispatch.h"
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace totk::recompiler {

// 2-level Radix lookup table for guest addresses (4-byte aligned instructions)
// Level 1: bits [39:20] (up to 1M entries, allocated on demand)
// Level 2: bits [19:2]  (256K pointers)
static constexpr size_t L1_SIZE = 1048576;
static constexpr size_t L2_SIZE = 262144;
static RecompiledFunc **g_radix_l1 = nullptr;

// Fallback unordered_map for addresses outside standard module base
static std::unordered_map<uint64_t, RecompiledFunc> g_fallback_map;

void init_dispatch_table() {
    if (!g_radix_l1) {
        g_radix_l1 = static_cast<RecompiledFunc **>(std::calloc(L1_SIZE, sizeof(RecompiledFunc *)));
    }
}

void register_recompiled_func(uint64_t guest_addr, RecompiledFunc fn) {
    init_dispatch_table();

    uint64_t l1_idx = (guest_addr >> 20) & (L1_SIZE - 1);
    uint64_t l2_idx = (guest_addr >> 2) & (L2_SIZE - 1);

    if (!g_radix_l1[l1_idx]) {
        g_radix_l1[l1_idx] = static_cast<RecompiledFunc *>(std::calloc(L2_SIZE, sizeof(RecompiledFunc)));
    }

    g_radix_l1[l1_idx][l2_idx] = fn;
    g_fallback_map[guest_addr] = fn;
}

void guest_indirect_call(Arm64Context *ctx, uint64_t target_addr) {
    if (g_radix_l1) {
        uint64_t l1_idx = (target_addr >> 20) & (L1_SIZE - 1);
        uint64_t l2_idx = (target_addr >> 2) & (L2_SIZE - 1);
        if (g_radix_l1[l1_idx] && g_radix_l1[l1_idx][l2_idx]) {
            g_radix_l1[l1_idx][l2_idx](ctx);
            return;
        }
    }

    auto it = g_fallback_map.find(target_addr);
    if (it != g_fallback_map.end() && it->second) {
        it->second(ctx);
        return;
    }

    std::fprintf(stderr, "[Dispatch Error] Unresolved indirect guest call to 0x%016llx (LR=0x%016llx)\n",
                 static_cast<unsigned long long>(target_addr),
                 static_cast<unsigned long long>(ctx->x[30]));
}

uint64_t resolve_v143_jump_table(uint64_t anchor_addr, int32_t index) {
    // Exact table offsets verified in totk-mobile/tools/patch_module.py
    switch (anchor_addr) {
        case 0x393ba0: {
            static const int32_t offsets[6] = {0x14, 0x20, 0x2c, 0x38, 0x44, 0x50};
            if (index >= 0 && index < 6) return anchor_addr + offsets[index];
            break;
        }
        case 0x596d20: {
            static const int32_t offsets[9] = {0x14, 0x24, 0x34, 0x44, 0x54, 0x64, 0x74, 0x84, 0x94};
            if (index >= 0 && index < 9) return anchor_addr + offsets[index];
            break;
        }
        case 0x598ee4: {
            static const int32_t offsets[9] = {0x10, 0x1c, 0x28, 0x34, 0x40, 0x4c, 0x58, 0x64, 0x70};
            if (index >= 0 && index < 9) return anchor_addr + offsets[index];
            break;
        }
        case 0x5f2a90: {
            static const int32_t offsets[5] = {0x14, 0x20, 0x2c, 0x38, 0x44};
            if (index >= 0 && index < 5) return anchor_addr + offsets[index];
            break;
        }
        case 0x6397cc: {
            static const int32_t offsets[7] = {0x14, 0x24, 0x34, 0x44, 0x54, 0x64, 0x74};
            if (index >= 0 && index < 7) return anchor_addr + offsets[index];
            break;
        }
        default:
            break;
    }
    return 0;
}

void dispatch_blr_x18(Arm64Context *ctx) {
    // AAPCS64: x16/x17 are scratch registers, x18 is platform register.
    // In TotK v1.4.3, blr x18 routes through Horizon IPC / HLE system calls.
    // LR (x30) contains the return address in the guest module.
    (void)ctx;
}

} // namespace totk::recompiler
