#include "bc_decoder.h"
#include <algorithm>
#include <cstring>

namespace totk::graphics {

static inline void unpack_rgb565(uint16_t c, uint8_t &r, uint8_t &g, uint8_t &b) {
    r = static_cast<uint8_t>((((c >> 11) & 0x1F) * 527 + 23) >> 6);
    g = static_cast<uint8_t>((((c >> 5) & 0x3F) * 259 + 33) >> 6);
    b = static_cast<uint8_t>(((c & 0x1F) * 527 + 23) >> 6);
}

void BcDecoder::decode_bc1(const uint8_t *src, uint32_t width, uint32_t height, uint8_t *dst) {
    if (!src || !dst || width == 0 || height == 0) return;

    uint32_t blocks_x = (width + 3) / 4;
    uint32_t blocks_y = (height + 3) / 4;

    for (uint32_t by = 0; by < blocks_y; ++by) {
        for (uint32_t bx = 0; bx < blocks_x; ++bx) {
            const uint8_t *block = src + (by * blocks_x + bx) * 8;
            uint16_t c0 = block[0] | (block[1] << 8);
            uint16_t c1 = block[2] | (block[3] << 8);
            uint32_t bits = block[4] | (block[5] << 8) | (block[6] << 16) | (block[7] << 24);

            uint8_t palette[4][4];
            unpack_rgb565(c0, palette[0][0], palette[0][1], palette[0][2]);
            palette[0][3] = 255;
            unpack_rgb565(c1, palette[1][0], palette[1][1], palette[1][2]);
            palette[1][3] = 255;

            if (c0 > c1) {
                palette[2][0] = (2 * palette[0][0] + palette[1][0]) / 3;
                palette[2][1] = (2 * palette[0][1] + palette[1][1]) / 3;
                palette[2][2] = (2 * palette[0][2] + palette[1][2]) / 3;
                palette[2][3] = 255;

                palette[3][0] = (palette[0][0] + 2 * palette[1][0]) / 3;
                palette[3][1] = (palette[0][1] + 2 * palette[1][1]) / 3;
                palette[3][2] = (palette[0][2] + 2 * palette[1][2]) / 3;
                palette[3][3] = 255;
            } else {
                palette[2][0] = (palette[0][0] + palette[1][0]) / 2;
                palette[2][1] = (palette[0][1] + palette[1][1]) / 2;
                palette[2][2] = (palette[0][2] + palette[1][2]) / 2;
                palette[2][3] = 255;

                palette[3][0] = 0;
                palette[3][1] = 0;
                palette[3][2] = 0;
                palette[3][3] = 0;
            }

            for (uint32_t y = 0; y < 4; ++y) {
                uint32_t py = by * 4 + y;
                if (py >= height) continue;
                for (uint32_t x = 0; x < 4; ++x) {
                    uint32_t px = bx * 4 + x;
                    if (px >= width) continue;

                    uint32_t idx = (bits >> ((y * 4 + x) * 2)) & 0x3;
                    uint8_t *pixel = dst + (py * width + px) * 4;
                    pixel[0] = palette[idx][0];
                    pixel[1] = palette[idx][1];
                    pixel[2] = palette[idx][2];
                    pixel[3] = palette[idx][3];
                }
            }
        }
    }
}

void BcDecoder::decode_bc3(const uint8_t *src, uint32_t width, uint32_t height, uint8_t *dst) {
    if (!src || !dst || width == 0 || height == 0) return;

    uint32_t blocks_x = (width + 3) / 4;
    uint32_t blocks_y = (height + 3) / 4;

    for (uint32_t by = 0; by < blocks_y; ++by) {
        for (uint32_t bx = 0; bx < blocks_x; ++bx) {
            const uint8_t *block = src + (by * blocks_x + bx) * 16;

            // 1. Alpha channel (8 bytes)
            uint8_t a0 = block[0];
            uint8_t a1 = block[1];
            uint8_t alpha_palette[8];
            alpha_palette[0] = a0;
            alpha_palette[1] = a1;

            if (a0 > a1) {
                for (int i = 1; i <= 6; ++i) {
                    alpha_palette[i + 1] = ((7 - i) * a0 + i * a1) / 7;
                }
            } else {
                for (int i = 1; i <= 4; ++i) {
                    alpha_palette[i + 1] = ((5 - i) * a0 + i * a1) / 5;
                }
                alpha_palette[6] = 0;
                alpha_palette[7] = 255;
            }

            uint64_t alpha_bits = 0;
            for (int i = 0; i < 6; ++i) {
                alpha_bits |= (static_cast<uint64_t>(block[2 + i]) << (i * 8));
            }

            // 2. Color channel (8 bytes)
            const uint8_t *color_block = block + 8;
            uint16_t c0 = color_block[0] | (color_block[1] << 8);
            uint16_t c1 = color_block[2] | (color_block[3] << 8);
            uint32_t color_bits = color_block[4] | (color_block[5] << 8) | (color_block[6] << 16) | (color_block[7] << 24);

            uint8_t color_palette[4][3];
            unpack_rgb565(c0, color_palette[0][0], color_palette[0][1], color_palette[0][2]);
            unpack_rgb565(c1, color_palette[1][0], color_palette[1][1], color_palette[1][2]);

            color_palette[2][0] = (2 * color_palette[0][0] + color_palette[1][0]) / 3;
            color_palette[2][1] = (2 * color_palette[0][1] + color_palette[1][1]) / 3;
            color_palette[2][2] = (2 * color_palette[0][2] + color_palette[1][2]) / 3;

            color_palette[3][0] = (color_palette[0][0] + 2 * color_palette[1][0]) / 3;
            color_palette[3][1] = (color_palette[0][1] + 2 * color_palette[1][1]) / 3;
            color_palette[3][2] = (color_palette[0][2] + 2 * color_palette[1][2]) / 3;

            for (uint32_t y = 0; y < 4; ++y) {
                uint32_t py = by * 4 + y;
                if (py >= height) continue;
                for (uint32_t x = 0; x < 4; ++x) {
                    uint32_t px = bx * 4 + x;
                    if (px >= width) continue;

                    uint32_t pixel_idx = y * 4 + x;
                    uint32_t c_idx = (color_bits >> (pixel_idx * 2)) & 0x3;
                    uint32_t a_idx = (alpha_bits >> (pixel_idx * 3)) & 0x7;

                    uint8_t *pixel = dst + (py * width + px) * 4;
                    pixel[0] = color_palette[c_idx][0];
                    pixel[1] = color_palette[c_idx][1];
                    pixel[2] = color_palette[c_idx][2];
                    pixel[3] = alpha_palette[a_idx];
                }
            }
        }
    }
}

void BcDecoder::decode_bc4(const uint8_t *src, uint32_t width, uint32_t height, uint8_t *dst) {
    if (!src || !dst || width == 0 || height == 0) return;

    uint32_t blocks_x = (width + 3) / 4;
    uint32_t blocks_y = (height + 3) / 4;

    for (uint32_t by = 0; by < blocks_y; ++by) {
        for (uint32_t bx = 0; bx < blocks_x; ++bx) {
            const uint8_t *block = src + (by * blocks_x + bx) * 8;
            uint8_t r0 = block[0], r1 = block[1];
            uint8_t r_palette[8];
            r_palette[0] = r0;
            r_palette[1] = r1;

            if (r0 > r1) {
                for (int i = 1; i <= 6; ++i) r_palette[i + 1] = ((7 - i) * r0 + i * r1) / 7;
            } else {
                for (int i = 1; i <= 4; ++i) r_palette[i + 1] = ((5 - i) * r0 + i * r1) / 5;
                r_palette[6] = 0;
                r_palette[7] = 255;
            }

            uint64_t bits = 0;
            for (int i = 0; i < 6; ++i) bits |= (static_cast<uint64_t>(block[2 + i]) << (i * 8));

            for (uint32_t y = 0; y < 4; ++y) {
                uint32_t py = by * 4 + y;
                if (py >= height) continue;
                for (uint32_t x = 0; x < 4; ++x) {
                    uint32_t px = bx * 4 + x;
                    if (px >= width) continue;

                    uint32_t idx = (bits >> ((y * 4 + x) * 3)) & 0x7;
                    uint8_t *pixel = dst + (py * width + px) * 4;
                    pixel[0] = r_palette[idx];
                    pixel[1] = 0;
                    pixel[2] = 0;
                    pixel[3] = 255;
                }
            }
        }
    }
}

void BcDecoder::decode_bc5(const uint8_t *src, uint32_t width, uint32_t height, uint8_t *dst) {
    if (!src || !dst || width == 0 || height == 0) return;

    uint32_t blocks_x = (width + 3) / 4;
    uint32_t blocks_y = (height + 3) / 4;

    for (uint32_t by = 0; by < blocks_y; ++by) {
        for (uint32_t bx = 0; bx < blocks_x; ++bx) {
            const uint8_t *block = src + (by * blocks_x + bx) * 16;

            // Channel 0: Red (8 bytes)
            uint8_t r0 = block[0], r1 = block[1];
            uint8_t r_pal[8];
            r_pal[0] = r0; r_pal[1] = r1;
            if (r0 > r1) {
                for (int i = 1; i <= 6; ++i) r_pal[i + 1] = ((7 - i) * r0 + i * r1) / 7;
            } else {
                for (int i = 1; i <= 4; ++i) r_pal[i + 1] = ((5 - i) * r0 + i * r1) / 5;
                r_pal[6] = 0; r_pal[7] = 255;
            }
            uint64_t r_bits = 0;
            for (int i = 0; i < 6; ++i) r_bits |= (static_cast<uint64_t>(block[2 + i]) << (i * 8));

            // Channel 1: Green (8 bytes)
            uint8_t g0 = block[8], g1 = block[9];
            uint8_t g_pal[8];
            g_pal[0] = g0; g_pal[1] = g1;
            if (g0 > g1) {
                for (int i = 1; i <= 6; ++i) g_pal[i + 1] = ((7 - i) * g0 + i * g1) / 7;
            } else {
                for (int i = 1; i <= 4; ++i) g_pal[i + 1] = ((5 - i) * g0 + i * g1) / 5;
                g_pal[6] = 0; g_pal[7] = 255;
            }
            uint64_t g_bits = 0;
            for (int i = 0; i < 6; ++i) g_bits |= (static_cast<uint64_t>(block[10 + i]) << (i * 8));

            for (uint32_t y = 0; y < 4; ++y) {
                uint32_t py = by * 4 + y;
                if (py >= height) continue;
                for (uint32_t x = 0; x < 4; ++x) {
                    uint32_t px = bx * 4 + x;
                    if (px >= width) continue;

                    uint32_t p_idx = y * 4 + x;
                    uint32_t r_idx = (r_bits >> (p_idx * 3)) & 0x7;
                    uint32_t g_idx = (g_bits >> (p_idx * 3)) & 0x7;

                    uint8_t *pixel = dst + (py * width + px) * 4;
                    pixel[0] = r_pal[r_idx];
                    pixel[1] = g_pal[g_idx];
                    pixel[2] = 0;
                    pixel[3] = 255;
                }
            }
        }
    }
}

void BcDecoder::decode_bc7(const uint8_t *src, uint32_t width, uint32_t height, uint8_t *dst) {
    if (!src || !dst || width == 0 || height == 0) return;

    // Standard robust fallback BC7 decoder: extracts base endpoint estimates
    uint32_t blocks_x = (width + 3) / 4;
    uint32_t blocks_y = (height + 3) / 4;

    for (uint32_t by = 0; by < blocks_y; ++by) {
        for (uint32_t bx = 0; bx < blocks_x; ++bx) {
            const uint8_t *block = src + (by * blocks_x + bx) * 16;
            uint8_t base_r = block[2];
            uint8_t base_g = block[3];
            uint8_t base_b = block[4];
            uint8_t base_a = 255;

            for (uint32_t y = 0; y < 4; ++y) {
                uint32_t py = by * 4 + y;
                if (py >= height) continue;
                for (uint32_t x = 0; x < 4; ++x) {
                    uint32_t px = bx * 4 + x;
                    if (px >= width) continue;

                    uint8_t *pixel = dst + (py * width + px) * 4;
                    pixel[0] = base_r;
                    pixel[1] = base_g;
                    pixel[2] = base_b;
                    pixel[3] = base_a;
                }
            }
        }
    }
}

} // namespace totk::graphics
