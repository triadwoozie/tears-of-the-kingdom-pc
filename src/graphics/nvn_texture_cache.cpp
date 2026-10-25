#include "nvn_texture_cache.h"
#include "astc_decoder.h"
#include <cstdio>
#include <cstring>

namespace totk::graphics {

NvnTextureCache::NvnTextureCache(VkDevice device)
    : device_(device) {}

NvnTextureCache::~NvnTextureCache() {
    if (device_ != VK_NULL_HANDLE) {
        for (auto &[_, tex] : textures_) {
            if (tex.vk_view) vkDestroyImageView(device_, tex.vk_view, nullptr);
            if (tex.vk_image) vkDestroyImage(device_, tex.vk_image, nullptr);
            if (tex.vk_memory) vkFreeMemory(device_, tex.vk_memory, nullptr);
        }
    }
}

void NvnTextureCache::set_device(VkDevice device, VkPhysicalDevice physical_device) {
    device_ = device;
    physical_device_ = physical_device;
}

CachedTexture *NvnTextureCache::get_or_create_texture(uint64_t guest_addr, uint32_t width, uint32_t height, TextureFormat fmt, bool is_rt) {
    auto it = textures_.find(guest_addr);
    if (it != textures_.end()) {
        return &it->second;
    }

    CachedTexture tex{};
    tex.guest_addr = guest_addr;
    tex.width = width;
    tex.height = height;
    tex.format = fmt;
    tex.is_render_target = is_rt;
    tex.render_scale = is_rt ? global_render_scale_ : 1.0f;

    if (device_ != VK_NULL_HANDLE) {
        uint32_t eff_w = static_cast<uint32_t>(width * tex.render_scale);
        uint32_t eff_h = static_cast<uint32_t>(height * tex.render_scale);

        VkImageCreateInfo img_info{};
        img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img_info.imageType = VK_IMAGE_TYPE_2D;
        img_info.format = (fmt == TextureFormat::RGBA8_SRGB) ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        img_info.extent = {eff_w, eff_h, 1};
        img_info.mipLevels = 1;
        img_info.arrayLayers = 1;
        img_info.samples = VK_SAMPLE_COUNT_1_BIT;
        img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        img_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | (is_rt ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_TRANSFER_DST_BIT);

        vkCreateImage(device_, &img_info, nullptr, &tex.vk_image);

        VkMemoryRequirements mem_reqs;
        vkGetBufferMemoryRequirements(device_, reinterpret_cast<VkBuffer>(tex.vk_image), &mem_reqs);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_reqs.size;
        alloc_info.memoryTypeIndex = 0;

        vkAllocateMemory(device_, &alloc_info, nullptr, &tex.vk_memory);
        vkBindImageMemory(device_, tex.vk_image, tex.vk_memory, 0);

        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = tex.vk_image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = img_info.format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;

        vkCreateImageView(device_, &view_info, nullptr, &tex.vk_view);
    }

    textures_[guest_addr] = tex;
    return &textures_[guest_addr];
}

void NvnTextureCache::decompress_astc_to_rgba8(const uint8_t *astc_in, uint8_t *rgba_out, uint32_t width, uint32_t height) {
    if (!astc_in || !rgba_out || width == 0 || height == 0) return;
    uint32_t blocks_x = (width + 3) / 4;
    uint32_t blocks_y = (height + 3) / 4;
    size_t data_size = static_cast<size_t>(blocks_x) * blocks_y * 16;
    if (!AstcDecoder::decompress_image(astc_in, data_size, width, height, 4, 4, rgba_out)) {
        std::memset(rgba_out, 0xFF, width * height * 4);
    }
}

} // namespace totk::graphics
