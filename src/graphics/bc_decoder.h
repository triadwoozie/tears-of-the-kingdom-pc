#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace totk::graphics {

class BcDecoder {
public:
    // Decodes BC1 (DXT1, 8 bytes per 4x4 block) -> RGBA8
    static void decode_bc1(const uint8_t *src, uint32_t width, uint32_t height, uint8_t *dst);

    // Decodes BC3 (DXT5, 16 bytes per 4x4 block) -> RGBA8
    static void decode_bc3(const uint8_t *src, uint32_t width, uint32_t height, uint8_t *dst);

    // Decodes BC4 (ATI1N, 8 bytes per 4x4 block) -> RGBA8
    static void decode_bc4(const uint8_t *src, uint32_t width, uint32_t height, uint8_t *dst);

    // Decodes BC5 (ATI2N, 16 bytes per 4x4 block) -> RGBA8
    static void decode_bc5(const uint8_t *src, uint32_t width, uint32_t height, uint8_t *dst);

    // Decodes BC7 (BPTC, 16 bytes per 4x4 block) -> RGBA8
    static void decode_bc7(const uint8_t *src, uint32_t width, uint32_t height, uint8_t *dst);
};

} // namespace totk::graphics
