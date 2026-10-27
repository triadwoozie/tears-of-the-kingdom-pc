#include "graphics/nvn_bindless.h"
#include <cstdio>
#include <cstring>

namespace totk::graphics {

NvnBindlessManager &NvnBindlessManager::instance() {
    static NvnBindlessManager s_instance;
    return s_instance;
}

NvnBindlessManager::NvnBindlessManager() = default;

NvnBindlessManager::~NvnBindlessManager() {
    shutdown();
}

void NvnBindlessManager::bind_texture_pool(uint64_t pool_ptr, ptrdiff_t offset, int size) {
    bound_texture_pool_ = pool_ptr;
    bound_texture_offset_ = offset;
    bound_texture_size_ = size;
    bind_texture_pool_count_++;
}

void NvnBindlessManager::bind_sampler_pool(uint64_t pool_ptr, ptrdiff_t offset, int size) {
    bound_sampler_pool_ = pool_ptr;
    bound_sampler_offset_ = offset;
    bound_sampler_size_ = size;
    bind_sampler_pool_count_++;
}

void NvnBindlessManager::register_texture(uint32_t id, uint64_t nvn_texture, uint64_t nvn_view,
                                          VkImageView view, VkImage image) {
    TextureDescriptorEntry entry;
    entry.id = id;
    entry.nvn_texture_ptr = nvn_texture;
    entry.nvn_view_ptr = nvn_view;
    entry.image_view = view;
    entry.image = image;
    entry.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    entry.is_valid = true;

    registered_textures_[id] = entry;

    // If live Vulkan descriptor set is active and view is valid, update descriptor
    if (device_ != VK_NULL_HANDLE && desc_set_ != VK_NULL_HANDLE && view != VK_NULL_HANDLE &&
        id < MAX_BINDLESS_TEXTURES) {
        VkDescriptorImageInfo image_info{};
        image_info.imageView = view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = desc_set_;
        write.dstBinding = 0;
        write.dstArrayElement = id;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.descriptorCount = 1;
        write.pImageInfo = &image_info;

        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
}

void NvnBindlessManager::register_sampler(uint32_t id, uint64_t nvn_sampler, VkSampler sampler) {
    SamplerDescriptorEntry entry;
    entry.id = id;
    entry.nvn_sampler_ptr = nvn_sampler;
    entry.sampler = sampler;
    entry.is_valid = true;

    registered_samplers_[id] = entry;

    // If live Vulkan descriptor set is active and sampler is valid, update descriptor
    if (device_ != VK_NULL_HANDLE && desc_set_ != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE &&
        id < MAX_BINDLESS_SAMPLERS) {
        VkDescriptorImageInfo sampler_info{};
        sampler_info.sampler = sampler;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = desc_set_;
        write.dstBinding = 1;
        write.dstArrayElement = id;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &sampler_info;

        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
}

const TextureDescriptorEntry *NvnBindlessManager::get_texture(uint32_t id) const {
    auto it = registered_textures_.find(id);
    if (it != registered_textures_.end()) {
        return &it->second;
    }
    return nullptr;
}

const SamplerDescriptorEntry *NvnBindlessManager::get_sampler(uint32_t id) const {
    auto it = registered_samplers_.find(id);
    if (it != registered_samplers_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool NvnBindlessManager::initialize(VkDevice device) {
    shutdown();
    device_ = device;

    if (device_ == VK_NULL_HANDLE) {
        // Headless / mock initialization
        initialized_ = true;
        return true;
    }

    // 1. Create Descriptor Set Layout with VK_EXT_descriptor_indexing flags
    VkDescriptorSetLayoutBinding bindings[2]{};

    // Binding 0: Sampled Images
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[0].descriptorCount = MAX_BINDLESS_TEXTURES;
    bindings[0].stageFlags = VK_SHADER_STAGE_ALL;

    // Binding 1: Samplers
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[1].descriptorCount = MAX_BINDLESS_SAMPLERS;
    bindings[1].stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorBindingFlags binding_flags[2] = {
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info{};
    flags_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    flags_info.bindingCount = 2;
    flags_info.pBindingFlags = binding_flags;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.pNext = &flags_info;
    layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layout_info.bindingCount = 2;
    layout_info.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &desc_layout_) != VK_SUCCESS) {
        std::fprintf(stderr, "[NvnBindless] Failed to create bindless descriptor set layout\n");
        return false;
    }

    // 2. Create Descriptor Pool
    VkDescriptorPoolSize pool_sizes[2]{};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    pool_sizes[0].descriptorCount = MAX_BINDLESS_TEXTURES;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    pool_sizes[1].descriptorCount = MAX_BINDLESS_SAMPLERS;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = pool_sizes;

    if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &desc_pool_) != VK_SUCCESS) {
        std::fprintf(stderr, "[NvnBindless] Failed to create bindless descriptor pool\n");
        vkDestroyDescriptorSetLayout(device_, desc_layout_, nullptr);
        desc_layout_ = VK_NULL_HANDLE;
        return false;
    }

    // 3. Allocate Descriptor Set
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = desc_pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &desc_layout_;

    if (vkAllocateDescriptorSets(device_, &alloc_info, &desc_set_) != VK_SUCCESS) {
        std::fprintf(stderr, "[NvnBindless] Failed to allocate bindless descriptor set\n");
        vkDestroyDescriptorPool(device_, desc_pool_, nullptr);
        desc_pool_ = VK_NULL_HANDLE;
        vkDestroyDescriptorSetLayout(device_, desc_layout_, nullptr);
        desc_layout_ = VK_NULL_HANDLE;
        return false;
    }

    initialized_ = true;
    return true;
}

void NvnBindlessManager::shutdown() {
    if (device_ != VK_NULL_HANDLE) {
        if (desc_pool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, desc_pool_, nullptr);
            desc_pool_ = VK_NULL_HANDLE;
            desc_set_ = VK_NULL_HANDLE;
        }
        if (desc_layout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, desc_layout_, nullptr);
            desc_layout_ = VK_NULL_HANDLE;
        }
        device_ = VK_NULL_HANDLE;
    }
    initialized_ = false;
}

void NvnBindlessManager::cmd_bind_bindless_set(VkCommandBuffer cmd, VkPipelineLayout layout,
                                               VkPipelineBindPoint bind_point,
                                               uint32_t set_index) {
    if (cmd != VK_NULL_HANDLE && layout != VK_NULL_HANDLE && desc_set_ != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, bind_point, layout, set_index, 1, &desc_set_, 0, nullptr);
    }
}

void NvnBindlessManager::reset() {
    bound_texture_pool_ = 0;
    bound_texture_offset_ = 0;
    bound_texture_size_ = 0;
    bound_sampler_pool_ = 0;
    bound_sampler_offset_ = 0;
    bound_sampler_size_ = 0;
    registered_textures_.clear();
    registered_samplers_.clear();
    bind_texture_pool_count_ = 0;
    bind_sampler_pool_count_ = 0;
    handle_query_count_ = 0;
}

} // namespace totk::graphics
