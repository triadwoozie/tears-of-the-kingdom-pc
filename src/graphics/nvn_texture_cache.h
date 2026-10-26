#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include "volk.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace totk::graphics {

enum class TextureFormat {
    RGBA8_UNORM = 0x25,
    RGBA8_SRGB = 0x38,
    ASTC_4x4_UNORM = 0x79,
    ASTC_4x4_SRGB = 0x7A,
    ASTC_5x4_UNORM = 0x7B,
    ASTC_5x4_SRGB = 0x7C,
    ASTC_5x5_UNORM = 0x7D,
    ASTC_5x5_SRGB = 0x7E,
    ASTC_6x5_UNORM = 0x7F,
    ASTC_6x5_SRGB = 0x80,
    ASTC_6x6_UNORM = 0x81,
    ASTC_6x6_SRGB = 0x82,
    ASTC_8x5_UNORM = 0x83,
    ASTC_8x5_SRGB = 0x84,
    ASTC_8x6_UNORM = 0x85,
    ASTC_8x6_SRGB = 0x86,
    ASTC_8x8_UNORM = 0x87,
    ASTC_8x8_SRGB = 0x88,
    ASTC_10x5_UNORM = 0x89,
    ASTC_10x5_SRGB = 0x8A,
    ASTC_10x6_UNORM = 0x8B,
    ASTC_10x6_SRGB = 0x8C,
    ASTC_10x8_UNORM = 0x8D,
    ASTC_10x8_SRGB = 0x8E,
    ASTC_10x10_UNORM = 0x8F,
    ASTC_10x10_SRGB = 0x90,
    ASTC_12x10_UNORM = 0x91,
    ASTC_12x10_SRGB = 0x92,
    ASTC_12x12_UNORM = 0x93,
    ASTC_12x12_SRGB = 0x94,
    BC1_RGBA_UNORM = 0x42,
    BC3_RGBA_UNORM = 0x44,
    BC7_UNORM = 0x4D,
    DEPTH24_STENCIL8 = 0x15,
    DEPTH32F = 0x16
};

inline bool get_astc_block_dim(TextureFormat fmt, uint32_t &w, uint32_t &h, bool &is_srgb) {
    switch (fmt) {
        case TextureFormat::ASTC_4x4_UNORM:   w = 4; h = 4; is_srgb = false; return true;
        case TextureFormat::ASTC_4x4_SRGB:    w = 4; h = 4; is_srgb = true;  return true;
        case TextureFormat::ASTC_5x4_UNORM:   w = 5; h = 4; is_srgb = false; return true;
        case TextureFormat::ASTC_5x4_SRGB:    w = 5; h = 4; is_srgb = true;  return true;
        case TextureFormat::ASTC_5x5_UNORM:   w = 5; h = 5; is_srgb = false; return true;
        case TextureFormat::ASTC_5x5_SRGB:    w = 5; h = 5; is_srgb = true;  return true;
        case TextureFormat::ASTC_6x5_UNORM:   w = 6; h = 5; is_srgb = false; return true;
        case TextureFormat::ASTC_6x5_SRGB:    w = 6; h = 5; is_srgb = true;  return true;
        case TextureFormat::ASTC_6x6_UNORM:   w = 6; h = 6; is_srgb = false; return true;
        case TextureFormat::ASTC_6x6_SRGB:    w = 6; h = 6; is_srgb = true;  return true;
        case TextureFormat::ASTC_8x5_UNORM:   w = 8; h = 5; is_srgb = false; return true;
        case TextureFormat::ASTC_8x5_SRGB:    w = 8; h = 5; is_srgb = true;  return true;
        case TextureFormat::ASTC_8x6_UNORM:   w = 8; h = 6; is_srgb = false; return true;
        case TextureFormat::ASTC_8x6_SRGB:    w = 8; h = 6; is_srgb = true;  return true;
        case TextureFormat::ASTC_8x8_UNORM:   w = 8; h = 8; is_srgb = false; return true;
        case TextureFormat::ASTC_8x8_SRGB:    w = 8; h = 8; is_srgb = true;  return true;
        case TextureFormat::ASTC_10x5_UNORM:  w = 10; h = 5; is_srgb = false; return true;
        case TextureFormat::ASTC_10x5_SRGB:   w = 10; h = 5; is_srgb = true;  return true;
        case TextureFormat::ASTC_10x6_UNORM:  w = 10; h = 6; is_srgb = false; return true;
        case TextureFormat::ASTC_10x6_SRGB:   w = 10; h = 6; is_srgb = true;  return true;
        case TextureFormat::ASTC_10x8_UNORM:  w = 10; h = 8; is_srgb = false; return true;
        case TextureFormat::ASTC_10x8_SRGB:   w = 10; h = 8; is_srgb = true;  return true;
        case TextureFormat::ASTC_10x10_UNORM: w = 10; h = 10; is_srgb = false; return true;
        case TextureFormat::ASTC_10x10_SRGB:  w = 10; h = 10; is_srgb = true;  return true;
        case TextureFormat::ASTC_12x10_UNORM: w = 12; h = 10; is_srgb = false; return true;
        case TextureFormat::ASTC_12x10_SRGB:  w = 12; h = 10; is_srgb = true;  return true;
        case TextureFormat::ASTC_12x12_UNORM: w = 12; h = 12; is_srgb = false; return true;
        case TextureFormat::ASTC_12x12_SRGB:  w = 12; h = 12; is_srgb = true;  return true;
        default: return false;
    }
}

struct CachedTexture {
    uint64_t guest_addr = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mip_levels = 1;
    TextureFormat format = TextureFormat::RGBA8_UNORM;
    VkImage vk_image = VK_NULL_HANDLE;
    VkImageView vk_view = VK_NULL_HANDLE;
    VkDeviceMemory vk_memory = VK_NULL_HANDLE;
    bool is_render_target = false;
    float render_scale = 1.0f;
};

class NvnTextureCache {
public:
    explicit NvnTextureCache(VkDevice device = VK_NULL_HANDLE);
    ~NvnTextureCache();

    void set_device(VkDevice device, VkPhysicalDevice physical_device);
    void set_global_render_scale(float scale) { global_render_scale_ = scale; }

    CachedTexture *get_or_create_texture(uint64_t guest_addr, uint32_t width, uint32_t height, TextureFormat fmt, bool is_rt = false);
    void decompress_astc_to_rgba8(const uint8_t *astc_in, uint8_t *rgba_out, uint32_t width, uint32_t height, uint32_t block_w = 4, uint32_t block_h = 4, bool is_srgb = false);

    size_t get_cached_count() const { return textures_.size(); }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    float global_render_scale_ = 1.0f;
    std::unordered_map<uint64_t, CachedTexture> textures_;
};

} // namespace totk::graphics
