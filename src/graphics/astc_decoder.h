#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace totk::graphics {

struct AstcBlockDim {
    uint32_t x = 4;
    uint32_t y = 4;
    uint32_t z = 1;
};

class AstcDecoder {
public:
    static bool is_block_dim_supported(uint32_t block_w, uint32_t block_h, uint32_t block_d = 1);

    // Decompress a 2D image of ASTC blocks into RGBA8 (4 bytes per pixel)
    // out_rgba must have capacity for width * height * 4 bytes
    static bool decompress_image(
        const uint8_t *astc_data,
        size_t astc_data_size,
        uint32_t width,
        uint32_t height,
        uint32_t block_w,
        uint32_t block_h,
        uint8_t *out_rgba,
        bool is_srgb = false
    );

    // Decompress a single 16-byte ASTC block into an RGBA8 buffer with specified row pitch
    static bool decompress_block(
        const uint8_t block_bytes[16],
        uint32_t block_w,
        uint32_t block_h,
        uint8_t *out_block_pixels,
        uint32_t row_pitch_bytes,
        bool is_srgb = false
    );

    // Helper: generate a synthetic void-extent ASTC block (solid color)
    static void make_void_extent_block(uint8_t block_bytes[16], uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    // Helper: generate a synthetic 2-endpoint gradient block
    static void make_linear_gradient_block(uint8_t block_bytes[16],
                                          uint8_t r0, uint8_t g0, uint8_t b0, uint8_t a0,
                                          uint8_t r1, uint8_t g1, uint8_t b1, uint8_t a1);
};

} // namespace totk::graphics
