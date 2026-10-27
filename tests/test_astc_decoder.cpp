#include "graphics/astc_decoder.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

void test_astc_block_dimensions() {
    std::printf("[Test] Running test_astc_block_dimensions...\n");
    assert(totk::graphics::AstcDecoder::is_block_dim_supported(4, 4));
    assert(totk::graphics::AstcDecoder::is_block_dim_supported(5, 5));
    assert(totk::graphics::AstcDecoder::is_block_dim_supported(6, 6));
    assert(totk::graphics::AstcDecoder::is_block_dim_supported(8, 8));
    assert(totk::graphics::AstcDecoder::is_block_dim_supported(10, 10));
    assert(totk::graphics::AstcDecoder::is_block_dim_supported(12, 12));
    assert(!totk::graphics::AstcDecoder::is_block_dim_supported(4, 4, 2)); // 3D unsupported
    std::printf("  -> ASTC Block Dimensions: PASSED\n");
}

void test_astc_4x4_void_extent() {
    std::printf("[Test] Running test_astc_4x4_void_extent...\n");
    uint8_t block[16];
    totk::graphics::AstcDecoder::make_void_extent_block(block, 210, 140, 70, 255);

    uint8_t pixels[4 * 4 * 4]{};
    bool ok = totk::graphics::AstcDecoder::decompress_block(block, 4, 4, pixels, 4 * 4);
    assert(ok);

    for (int i = 0; i < 16; i++) {
        assert(pixels[i * 4 + 0] == 210);
        assert(pixels[i * 4 + 1] == 140);
        assert(pixels[i * 4 + 2] == 70);
        assert(pixels[i * 4 + 3] == 255);
    }
    std::printf("  -> ASTC 4x4 Void-Extent Block: PASSED\n");
}

void test_astc_8x8_void_extent() {
    std::printf("[Test] Running test_astc_8x8_void_extent...\n");
    uint8_t block[16];
    totk::graphics::AstcDecoder::make_void_extent_block(block, 15, 80, 220, 200);

    uint8_t pixels[8 * 8 * 4]{};
    bool ok = totk::graphics::AstcDecoder::decompress_block(block, 8, 8, pixels, 8 * 4);
    assert(ok);

    for (int i = 0; i < 64; i++) {
        assert(pixels[i * 4 + 0] == 15);
        assert(pixels[i * 4 + 1] == 80);
        assert(pixels[i * 4 + 2] == 220);
        assert(pixels[i * 4 + 3] == 200);
    }
    std::printf("  -> ASTC 8x8 Void-Extent Block: PASSED\n");
}

void test_astc_linear_gradient() {
    std::printf("[Test] Running test_astc_linear_gradient...\n");
    uint8_t block[16];
    // Gradient from black (0, 0, 0, 255) to white (255, 255, 255, 255)
    totk::graphics::AstcDecoder::make_linear_gradient_block(block,
                                                            0, 0, 0, 255,
                                                            255, 255, 255, 255);

    uint8_t pixels[4 * 4 * 4]{};
    bool ok = totk::graphics::AstcDecoder::decompress_block(block, 4, 4, pixels, 4 * 4);
    assert(ok);

    // Top-left pixel should be close to endpoint 0
    assert(pixels[0] < 50);
    // Bottom-right pixel should be close to endpoint 1
    assert(pixels[15 * 4 + 0] > 200);
    // Alpha preserved
    assert(pixels[3] == 255);
    assert(pixels[15 * 4 + 3] == 255);

    std::printf("  -> ASTC Linear Gradient (SIMDe Blending): PASSED\n");
}

void test_astc_image_decompression() {
    std::printf("[Test] Running test_astc_image_decompression...\n");
    // 16x16 image = 4x4 blocks of 4x4 pixels = 16 blocks = 256 bytes
    std::vector<uint8_t> compressed(16 * 16);
    for (int b = 0; b < 16; b++) {
        uint8_t r = static_cast<uint8_t>(b * 16);
        totk::graphics::AstcDecoder::make_void_extent_block(&compressed[b * 16], r, 100, 200, 255);
    }

    std::vector<uint8_t> out_rgba(16 * 16 * 4, 0);
    bool ok = totk::graphics::AstcDecoder::decompress_image(
        compressed.data(), compressed.size(), 16, 16, 4, 4, out_rgba.data()
    );
    assert(ok);

    // Check block 0 top-left
    assert(out_rgba[0] == 0);
    assert(out_rgba[1] == 100);
    assert(out_rgba[2] == 200);
    assert(out_rgba[3] == 255);

    // Check block 15 bottom-right
    size_t last_idx = (15 * 16 + 15) * 4;
    assert(out_rgba[last_idx + 0] == 240);
    assert(out_rgba[last_idx + 1] == 100);
    assert(out_rgba[last_idx + 2] == 200);
    assert(out_rgba[last_idx + 3] == 255);

    // Test non-multiple image dimension (e.g. 15x15)
    std::vector<uint8_t> out_rgba_15(15 * 15 * 4, 0);
    bool ok_15 = totk::graphics::AstcDecoder::decompress_image(
        compressed.data(), compressed.size(), 15, 15, 4, 4, out_rgba_15.data()
    );
    assert(ok_15);

    std::printf("  -> ASTC Full Image Decompression: PASSED\n");
}

void test_astc_all_supported_dimensions() {
    std::printf("[Test] Running test_astc_all_supported_dimensions...\n");
    const std::pair<uint32_t, uint32_t> all_dims[] = {
        {4, 4}, {5, 4}, {5, 5}, {6, 5}, {6, 6},
        {8, 5}, {8, 6}, {8, 8}, {10, 5}, {10, 6},
        {10, 8}, {10, 10}, {12, 10}, {12, 12}
    };

    uint8_t block[16];
    totk::graphics::AstcDecoder::make_void_extent_block(block, 128, 64, 32, 255);

    for (const auto &[bw, bh] : all_dims) {
        assert(totk::graphics::AstcDecoder::is_block_dim_supported(bw, bh));
        std::vector<uint8_t> pixels(bw * bh * 4, 0);
        bool ok = totk::graphics::AstcDecoder::decompress_block(block, bw, bh, pixels.data(), bw * 4);
        assert(ok);
        assert(pixels[0] == 128);
        assert(pixels[1] == 64);
        assert(pixels[2] == 32);
        assert(pixels[3] == 255);
    }

    std::printf("  -> All Switch ASTC Dimensions (4x4 through 12x12): PASSED\n");
}

void test_astc_srgb_conversion() {
    std::printf("[Test] Running test_astc_srgb_conversion...\n");
    uint8_t block[16];
    // Mid-gray sRGB value ~128
    totk::graphics::AstcDecoder::make_void_extent_block(block, 128, 128, 128, 255);

    uint8_t linear_pixels[4 * 4 * 4]{};
    bool ok_lin = totk::graphics::AstcDecoder::decompress_block(block, 4, 4, linear_pixels, 16, false);
    assert(ok_lin);
    assert(linear_pixels[0] == 128);

    uint8_t srgb_pixels[4 * 4 * 4]{};
    bool ok_srgb = totk::graphics::AstcDecoder::decompress_block(block, 4, 4, srgb_pixels, 16, true);
    assert(ok_srgb);
    // In linear space, 128 sRGB is ~0.216 linear -> ~55/255
    assert(srgb_pixels[0] < 70 && srgb_pixels[0] > 40);
    // Alpha channel is always unmultiplied linear
    assert(srgb_pixels[3] == 255);

    std::printf("  -> ASTC sRGB-to-Linear Gamma Conversion: PASSED\n");
}

int main() {
    std::printf("==================================================\n");
    std::printf("  ASTC Texture Decoder Unit Tests\n");
    std::printf("==================================================\n");

    test_astc_block_dimensions();
    test_astc_4x4_void_extent();
    test_astc_8x8_void_extent();
    test_astc_linear_gradient();
    test_astc_image_decompression();
    test_astc_all_supported_dimensions();
    test_astc_srgb_conversion();

    std::printf("\nAll ASTC Decoder unit tests PASSED successfully!\n");
    return 0;
}
