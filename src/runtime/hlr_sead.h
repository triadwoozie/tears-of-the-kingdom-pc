#pragma once

#include "guest_abi/sead_framework_abi.h"
#include <cstddef>
#include <cstdint>

namespace totk::hlr {

// AVX2 / FMA accelerated Matrix34 * Vector3
void sead_matrix34f_mult(const sead::Matrix34f &m, const sead::Vector3f &v, sead::Vector3f &out);

// AVX2 accelerated Vector3 normalization
void sead_vector3f_normalize(sead::Vector3f &v);

// Hardware-accelerated CRC32 matching Nintendo's polynomial
uint32_t sead_calc_crc32(const void *data, size_t size);

// Register permanent HLR hooks for sead heaps, memory allocators, and math
void register_sead_hlr_hooks();

} // namespace totk::hlr
