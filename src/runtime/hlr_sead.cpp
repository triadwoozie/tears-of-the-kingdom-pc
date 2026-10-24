#include "hlr_sead.h"
#include <cmath>
#include <immintrin.h>

namespace totk::hlr {

void sead_matrix34f_mult(const sead::Matrix34f &m, const sead::Vector3f &v, sead::Vector3f &out) {
    // Row 0
    __m128 r0 = _mm_set_ps(m.m[0][3], m.m[0][2], m.m[0][1], m.m[0][0]);
    // Row 1
    __m128 r1 = _mm_set_ps(m.m[1][3], m.m[1][2], m.m[1][1], m.m[1][0]);
    // Row 2
    __m128 r2 = _mm_set_ps(m.m[2][3], m.m[2][2], m.m[2][1], m.m[2][0]);

    __m128 v_vec = _mm_set_ps(1.0f, v.z, v.y, v.x);

    __m128 prod0 = _mm_mul_ps(r0, v_vec);
    __m128 prod1 = _mm_mul_ps(r1, v_vec);
    __m128 prod2 = _mm_mul_ps(r2, v_vec);

    // Horizontal adds for each row
    __m128 hsum0 = _mm_hadd_ps(prod0, prod0);
    hsum0 = _mm_hadd_ps(hsum0, hsum0);

    __m128 hsum1 = _mm_hadd_ps(prod1, prod1);
    hsum1 = _mm_hadd_ps(hsum1, hsum1);

    __m128 hsum2 = _mm_hadd_ps(prod2, prod2);
    hsum2 = _mm_hadd_ps(hsum2, hsum2);

    out.x = _mm_cvtss_f32(hsum0);
    out.y = _mm_cvtss_f32(hsum1);
    out.z = _mm_cvtss_f32(hsum2);
}

void sead_vector3f_normalize(sead::Vector3f &v) {
    float len_sq = v.x * v.x + v.y * v.y + v.z * v.z;
    if (len_sq <= 1e-12f) return;

    __m128 s = _mm_set_ss(len_sq);
    __m128 inv = _mm_rsqrt_ss(s);
    float inv_len = _mm_cvtss_f32(inv);

    // One iteration of Newton-Raphson refinement
    inv_len = 0.5f * inv_len * (3.0f - len_sq * inv_len * inv_len);

    v.x *= inv_len;
    v.y *= inv_len;
    v.z *= inv_len;
}

uint32_t sead_calc_crc32(const void *data, size_t size) {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    uint32_t crc = ~0u;

    // Process 8-byte blocks via SSE4.2 / x86_64 crc32
    while (size >= 8) {
        uint64_t val;
        __builtin_memcpy(&val, p, 8);
        crc = static_cast<uint32_t>(_mm_crc32_u64(crc, val));
        p += 8;
        size -= 8;
    }

    while (size > 0) {
        crc = _mm_crc32_u8(crc, *p);
        p++;
        size--;
    }

    return ~crc;
}

} // namespace totk::hlr
