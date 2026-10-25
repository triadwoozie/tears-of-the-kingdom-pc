#include "recompiler/arm64_ctx.h"
#include "recompiler/dispatch.h"
#include "hlr_sead.h"
#include <cassert>
#include <cmath>
#include <cstdio>

int main() {
    std::printf("[Test] Testing AArch64 Context, Dispatch & SIMDe Operations...\n");

    totk::recompiler::Arm64Context ctx;
    totk::recompiler::init_arm64_context(&ctx, 0x7100080000ull, 0x7100000000ull);

    // Verify context initialization
    assert(ctx.sp == 0x7100080000ull);
    assert(ctx.tpidrro_el0 == 0x7100000000ull);
    assert(ctx.x[0] == 0);
    assert(ctx.v[0] == 0);

    // Test SIMDe NEON vector addition
    simde_float32x4_t a = simde_vsetq_lane_f32(1.0f, simde_vdupq_n_f32(0.0f), 0);
    a = simde_vsetq_lane_f32(2.0f, a, 1);
    a = simde_vsetq_lane_f32(3.0f, a, 2);
    a = simde_vsetq_lane_f32(4.0f, a, 3);

    simde_float32x4_t b = simde_vdupq_n_f32(10.0f);
    simde_float32x4_t sum = simde_vaddq_f32(a, b);

    *reinterpret_cast<simde_float32x4_t *>(&ctx.v[0]) = sum;
    const float *f32 = reinterpret_cast<const float *>(&ctx.v[0]);
    assert(f32[0] == 11.0f);
    assert(f32[1] == 12.0f);
    assert(f32[2] == 13.0f);
    assert(f32[3] == 14.0f);

    // Test Jump Table resolution for v1.4.3
    uint64_t target = totk::recompiler::resolve_v143_jump_table(0x393ba0, 0);
    assert(target == 0x393ba0 + 0x14);

    // Test HLR Vector3f normalization
    sead::Vector3f vec{3.0f, 4.0f, 0.0f};
    totk::hlr::sead_vector3f_normalize(vec);
    assert(std::fabs(vec.x - 0.6f) < 0.01f);
    assert(std::fabs(vec.y - 0.8f) < 0.01f);
    assert(std::fabs(vec.z - 0.0f) < 0.001f);

    // Test HLR Matrix34f multiply
    sead::Matrix34f mat{};
    sead::Vector3f in_vec{1.0f, 2.0f, 3.0f};
    sead::Vector3f out_vec{};
    totk::hlr::sead_matrix34f_mult(mat, in_vec, out_vec);
    assert(std::fabs(out_vec.x - 1.0f) < 1e-4f);
    assert(std::fabs(out_vec.y - 2.0f) < 1e-4f);
    assert(std::fabs(out_vec.z - 3.0f) < 1e-4f);

    // Test HLR CRC32
    const char test_str[] = "Actor_Link";
    uint32_t crc = totk::hlr::sead_calc_crc32(test_str, sizeof(test_str) - 1);
    assert(crc != 0);

    std::printf("[Test] AArch64 Context, Dispatch & SIMDe Operations verified successfully!\n");
    return 0;
}
