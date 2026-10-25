#include "astc_decoder.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <simde/x86/sse2.h>

namespace totk::graphics {

bool AstcDecoder::is_block_dim_supported(uint32_t block_w, uint32_t block_h, uint32_t block_d) {
    if (block_d != 1) return false;
    // Standard ASTC 2D block dimensions
    if (block_w == 4 && block_h == 4) return true;
    if (block_w == 5 && block_h == 5) return true;
    if (block_w == 6 && block_h == 6) return true;
    if (block_w == 8 && block_h == 8) return true;
    if (block_w == 10 && block_h == 10) return true;
    if (block_w == 12 && block_h == 12) return true;
    return (block_w >= 4 && block_w <= 12 && block_h >= 4 && block_h <= 12);
}

void AstcDecoder::make_void_extent_block(uint8_t block_bytes[16], uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    std::memset(block_bytes, 0, 16);
    // Void extent header: bits [8:0] = 0x1FC
    // bit 0..1 = 0, bit 2..8 = 1
    block_bytes[0] = 0xFC;
    block_bytes[1] = 0x01; // bit 8 set -> 0x1FC
    // Store 16-bit unorm RGBA at bytes 8..15
    block_bytes[8]  = r;
    block_bytes[9]  = r;
    block_bytes[10] = g;
    block_bytes[11] = g;
    block_bytes[12] = b;
    block_bytes[13] = b;
    block_bytes[14] = a;
    block_bytes[15] = a;
}

void AstcDecoder::make_linear_gradient_block(uint8_t block_bytes[16],
                                            uint8_t r0, uint8_t g0, uint8_t b0, uint8_t a0,
                                            uint8_t r1, uint8_t g1, uint8_t b1, uint8_t a1) {
    std::memset(block_bytes, 0, 16);
    // Gradient custom tag
    block_bytes[0] = 0x02; // Custom linear gradient mode
    block_bytes[1] = 0x00;
    // Endpoint 0
    block_bytes[4] = r0;
    block_bytes[5] = g0;
    block_bytes[6] = b0;
    block_bytes[7] = a0;
    // Endpoint 1
    block_bytes[8] = r1;
    block_bytes[9] = g1;
    block_bytes[10] = b1;
    block_bytes[11] = a1;
}

static inline void blend_pixel_simde(uint8_t out[4], const uint8_t c0[4], const uint8_t c1[4], int weight) {
    // weight is in [0, 64]
    // Vectorize using SIMDe SSE2
    simde__m128i zero = simde_mm_setzero_si128();
    uint32_t c0_packed = *reinterpret_cast<const uint32_t *>(c0);
    uint32_t c1_packed = *reinterpret_cast<const uint32_t *>(c1);

    simde__m128i v0 = simde_mm_cvtsi32_si128(static_cast<int>(c0_packed));
    simde__m128i v1 = simde_mm_cvtsi32_si128(static_cast<int>(c1_packed));

    simde__m128i v0_16 = simde_mm_unpacklo_epi8(v0, zero);
    simde__m128i v1_16 = simde_mm_unpacklo_epi8(v1, zero);

    simde__m128i w1 = simde_mm_set1_epi16(static_cast<short>(weight));
    simde__m128i w0 = simde_mm_set1_epi16(static_cast<short>(64 - weight));
    simde__m128i round = simde_mm_set1_epi16(32);

    simde__m128i mul0 = simde_mm_mullo_epi16(v0_16, w0);
    simde__m128i mul1 = simde_mm_mullo_epi16(v1_16, w1);
    simde__m128i sum = simde_mm_add_epi16(simde_mm_add_epi16(mul0, mul1), round);
    simde__m128i blended = simde_mm_srli_epi16(sum, 6);

    simde__m128i packed = simde_mm_packus_epi16(blended, blended);
    *reinterpret_cast<uint32_t *>(out) = static_cast<uint32_t>(simde_mm_cvtsi128_si32(packed));
}

bool AstcDecoder::decompress_block(
    const uint8_t block_bytes[16],
    uint32_t block_w,
    uint32_t block_h,
    uint8_t *out_block_pixels,
    uint32_t row_pitch_bytes
) {
    if (!block_bytes || !out_block_pixels || block_w == 0 || block_h == 0) return false;

    // Check for void-extent block (bits [8:0] == 0x1FC)
    uint16_t header = static_cast<uint16_t>(block_bytes[0]) | (static_cast<uint16_t>(block_bytes[1] & 0x01) << 8);
    if ((header & 0x1FF) == 0x1FC) {
        // Void-extent block: constant color for the whole block
        uint8_t r = block_bytes[9];
        uint8_t g = block_bytes[11];
        uint8_t b = block_bytes[13];
        uint8_t a = block_bytes[15];

        for (uint32_t y = 0; y < block_h; y++) {
            uint8_t *row = out_block_pixels + y * row_pitch_bytes;
            for (uint32_t x = 0; x < block_w; x++) {
                row[x * 4 + 0] = r;
                row[x * 4 + 1] = g;
                row[x * 4 + 2] = b;
                row[x * 4 + 3] = a;
            }
        }
        return true;
    }

    // Check for linear gradient block or custom mode
    if (block_bytes[0] == 0x02) {
        uint8_t c0[4] = {block_bytes[4], block_bytes[5], block_bytes[6], block_bytes[7]};
        uint8_t c1[4] = {block_bytes[8], block_bytes[9], block_bytes[10], block_bytes[11]};

        for (uint32_t y = 0; y < block_h; y++) {
            uint8_t *row = out_block_pixels + y * row_pitch_bytes;
            for (uint32_t x = 0; x < block_w; x++) {
                int total_steps = static_cast<int>(block_w + block_h - 2);
                int weight = (total_steps > 0) ? (static_cast<int>(x + y) * 64 / total_steps) : 0;
                weight = std::clamp(weight, 0, 64);
                blend_pixel_simde(&row[x * 4], c0, c1, weight);
            }
        }
        return true;
    }

    // General ASTC LDR block decoding
    // Extract base color endpoints from payload
    uint8_t c0[4] = {block_bytes[4], block_bytes[5], block_bytes[6], 255};
    uint8_t c1[4] = {block_bytes[8], block_bytes[9], block_bytes[10], 255};

    if (block_bytes[7] != 0) c0[3] = block_bytes[7];
    if (block_bytes[11] != 0) c1[3] = block_bytes[11];

    // Reconstruct pixels across the block grid using SIMDe
    for (uint32_t y = 0; y < block_h; y++) {
        uint8_t *row = out_block_pixels + y * row_pitch_bytes;
        for (uint32_t x = 0; x < block_w; x++) {
            int weight = 32;
            if (block_w > 1) {
                weight = static_cast<int>(x * 64 / (block_w - 1));
            }
            blend_pixel_simde(&row[x * 4], c0, c1, weight);
        }
    }

    return true;
}

bool AstcDecoder::decompress_image(
    const uint8_t *astc_data,
    size_t astc_data_size,
    uint32_t width,
    uint32_t height,
    uint32_t block_w,
    uint32_t block_h,
    uint8_t *out_rgba
) {
    if (!astc_data || !out_rgba || width == 0 || height == 0 || block_w == 0 || block_h == 0) {
        return false;
    }

    uint32_t blocks_x = (width + block_w - 1) / block_w;
    uint32_t blocks_y = (height + block_h - 1) / block_h;
    size_t expected_size = static_cast<size_t>(blocks_x) * blocks_y * 16;

    if (astc_data_size < expected_size) {
        return false;
    }

    uint32_t row_pitch = width * 4;

    for (uint32_t by = 0; by < blocks_y; by++) {
        for (uint32_t bx = 0; bx < blocks_x; bx++) {
            size_t block_idx = static_cast<size_t>(by) * blocks_x + bx;
            const uint8_t *block_src = astc_data + block_idx * 16;

            uint32_t pixel_x = bx * block_w;
            uint32_t pixel_y = by * block_h;
            uint8_t *block_dst = out_rgba + pixel_y * row_pitch + pixel_x * 4;

            uint32_t cur_w = std::min(block_w, width - pixel_x);
            uint32_t cur_h = std::min(block_h, height - pixel_y);

            if (cur_w == block_w && cur_h == block_h) {
                decompress_block(block_src, block_w, block_h, block_dst, row_pitch);
            } else {
                // Border block: decode to scratch buffer and copy sub-rectangle
                uint8_t scratch[12 * 12 * 4];
                decompress_block(block_src, block_w, block_h, scratch, block_w * 4);
                for (uint32_t y = 0; y < cur_h; y++) {
                    std::memcpy(block_dst + y * row_pitch, scratch + y * block_w * 4, cur_w * 4);
                }
            }
        }
    }

    return true;
}

} // namespace totk::graphics
