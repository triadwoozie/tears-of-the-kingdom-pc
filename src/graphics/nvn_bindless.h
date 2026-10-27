#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include "volk.h"

namespace totk::graphics {

struct TextureDescriptorEntry {
    uint32_t id = 0;
    uint64_t nvn_texture_ptr = 0;
    uint64_t nvn_view_ptr = 0;
    VkImageView image_view = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bool is_valid = false;
};

struct SamplerDescriptorEntry {
    uint32_t id = 0;
    uint64_t nvn_sampler_ptr = 0;
    VkSampler sampler = VK_NULL_HANDLE;
    bool is_valid = false;
};

struct NvnSamplerBuilderInternal {
    uint32_t min_filter = 1; // LINEAR
    uint32_t mag_filter = 1; // LINEAR
    uint32_t mip_filter = 1; // LINEAR
    uint32_t wrap_s = 0;     // CLAMP_TO_EDGE
    uint32_t wrap_t = 0;
    uint32_t wrap_r = 0;
    float lod_min = 0.0f;
    float lod_max = 1000.0f;
    float lod_bias = 0.0f;
    float max_anisotropy = 1.0f;
    uint32_t compare_mode = 0;
    uint32_t compare_func = 0;
    uint32_t reduction_filter = 0; // AVERAGE
    float border_color[4]{0.0f, 0.0f, 0.0f, 0.0f};
};

struct NvnSamplerInternal {
    NvnSamplerBuilderInternal desc{};
    VkSampler vk_sampler = VK_NULL_HANDLE;
    uint32_t sampler_id = 0;
};

class NvnBindlessManager {
public:
    static constexpr uint32_t MAX_BINDLESS_TEXTURES = 4096;
    static constexpr uint32_t MAX_BINDLESS_SAMPLERS = 1024;

    static NvnBindlessManager &instance();

    // Packing & unpacking 64-bit NVN handles:
    // NVN format: (sampler_id << 20) | (texture_id & 0xFFFFF)
    static constexpr uint64_t pack_texture_handle(uint32_t texture_id, uint32_t sampler_id) {
        return (static_cast<uint64_t>(sampler_id & 0xFFF) << 20) | (static_cast<uint64_t>(texture_id) & 0xFFFFF);
    }

    static constexpr uint64_t pack_texel_fetch_handle(uint32_t texture_id) {
        return static_cast<uint64_t>(texture_id & 0xFFFFF);
    }

    static constexpr uint64_t pack_sampler_handle(uint32_t sampler_id) {
        return static_cast<uint64_t>(sampler_id & 0xFFF);
    }

    static constexpr uint32_t unpack_texture_id(uint64_t handle) {
        return static_cast<uint32_t>(handle & 0xFFFFF);
    }

    static constexpr uint32_t unpack_sampler_id(uint64_t handle) {
        return static_cast<uint32_t>((handle >> 20) & 0xFFF);
    }

    // Pool Tracking
    void bind_texture_pool(uint64_t pool_ptr, ptrdiff_t offset = 0, int size = 0);
    void bind_sampler_pool(uint64_t pool_ptr, ptrdiff_t offset = 0, int size = 0);
    uint64_t get_bound_texture_pool() const { return bound_texture_pool_; }
    uint64_t get_bound_sampler_pool() const { return bound_sampler_pool_; }
    ptrdiff_t get_bound_texture_offset() const { return bound_texture_offset_; }
    ptrdiff_t get_bound_sampler_offset() const { return bound_sampler_offset_; }
    int get_bound_texture_size() const { return bound_texture_size_; }
    int get_bound_sampler_size() const { return bound_sampler_size_; }

    // Texture / Sampler Registration
    void register_texture(uint32_t id, uint64_t nvn_texture, uint64_t nvn_view = 0,
                          VkImageView view = VK_NULL_HANDLE, VkImage image = VK_NULL_HANDLE);
    void register_sampler(uint32_t id, uint64_t nvn_sampler, VkSampler sampler = VK_NULL_HANDLE);

    const TextureDescriptorEntry *get_texture(uint32_t id) const;
    const SamplerDescriptorEntry *get_sampler(uint32_t id) const;
    size_t get_registered_texture_count() const { return registered_textures_.size(); }
    size_t get_registered_sampler_count() const { return registered_samplers_.size(); }

    // Vulkan Bindless Descriptors (VK_EXT_descriptor_indexing)
    bool initialize(VkDevice device = VK_NULL_HANDLE);
    void shutdown();

    void cmd_bind_bindless_set(VkCommandBuffer cmd, VkPipelineLayout layout,
                               VkPipelineBindPoint bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS,
                               uint32_t set_index = 0);

    VkDescriptorSetLayout get_descriptor_set_layout() const { return desc_layout_; }
    VkDescriptorSet get_descriptor_set() const { return desc_set_; }
    bool is_initialized() const { return initialized_; }

    void reset();

    // Stats
    uint64_t get_bind_texture_pool_count() const { return bind_texture_pool_count_; }
    uint64_t get_bind_sampler_pool_count() const { return bind_sampler_pool_count_; }
    uint64_t get_handle_query_count() const { return handle_query_count_; }
    void increment_handle_query_count() { handle_query_count_++; }

private:
    NvnBindlessManager();
    ~NvnBindlessManager();

    uint64_t bound_texture_pool_ = 0;
    ptrdiff_t bound_texture_offset_ = 0;
    int bound_texture_size_ = 0;

    uint64_t bound_sampler_pool_ = 0;
    ptrdiff_t bound_sampler_offset_ = 0;
    int bound_sampler_size_ = 0;

    std::unordered_map<uint32_t, TextureDescriptorEntry> registered_textures_;
    std::unordered_map<uint32_t, SamplerDescriptorEntry> registered_samplers_;

    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorPool desc_pool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout desc_layout_ = VK_NULL_HANDLE;
    VkDescriptorSet desc_set_ = VK_NULL_HANDLE;
    bool initialized_ = false;

    uint64_t bind_texture_pool_count_ = 0;
    uint64_t bind_sampler_pool_count_ = 0;
    uint64_t handle_query_count_ = 0;
};

} // namespace totk::graphics
