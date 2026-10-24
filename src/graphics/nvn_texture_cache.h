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
    BC1_RGBA_UNORM = 0x42,
    BC3_RGBA_UNORM = 0x44,
    BC7_UNORM = 0x4D,
    DEPTH24_STENCIL8 = 0x15,
    DEPTH32F = 0x16
};

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
    void decompress_astc_to_rgba8(const uint8_t *astc_in, uint8_t *rgba_out, uint32_t width, uint32_t height);

    size_t get_cached_count() const { return textures_.size(); }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    float global_render_scale_ = 1.0f;
    std::unordered_map<uint64_t, CachedTexture> textures_;
};

} // namespace totk::graphics
