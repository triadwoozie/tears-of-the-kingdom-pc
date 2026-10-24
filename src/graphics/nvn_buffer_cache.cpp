#include "nvn_buffer_cache.h"
#include <cstdio>
#include <cstring>

namespace totk::graphics {

NvnBufferCache::NvnBufferCache(VkDevice device)
    : device_(device) {}

NvnBufferCache::~NvnBufferCache() {
    if (device_ != VK_NULL_HANDLE) {
        for (auto &alloc : allocations_) {
            if (alloc.vk_buffer) vkDestroyBuffer(device_, alloc.vk_buffer, nullptr);
            if (alloc.vk_memory) vkFreeMemory(device_, alloc.vk_memory, nullptr);
        }
    }
}

void NvnBufferCache::set_device(VkDevice device, VkPhysicalDevice physical_device) {
    device_ = device;
    physical_device_ = physical_device;
}

BufferAllocation *NvnBufferCache::register_guest_buffer(uint64_t guest_addr, size_t size) {
    for (auto &alloc : allocations_) {
        if (alloc.guest_addr == guest_addr && alloc.size >= size) {
            return &alloc;
        }
    }

    BufferAllocation alloc{};
    alloc.guest_addr = guest_addr;
    alloc.size = size;
    alloc.is_dirty = true;

    if (device_ != VK_NULL_HANDLE) {
        VkBufferCreateInfo buf_info{};
        buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buf_info.size = size;
        buf_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(device_, &buf_info, nullptr, &alloc.vk_buffer);

        VkMemoryRequirements mem_reqs;
        vkGetBufferMemoryRequirements(device_, alloc.vk_buffer, &mem_reqs);

        VkMemoryAllocateInfo mem_alloc{};
        mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mem_alloc.allocationSize = mem_reqs.size;
        mem_alloc.memoryTypeIndex = 0; // Default host-visible memory type

        vkAllocateMemory(device_, &mem_alloc, nullptr, &alloc.vk_memory);
        vkBindBufferMemory(device_, alloc.vk_buffer, alloc.vk_memory, 0);
    }

    allocations_.push_back(alloc);
    return &allocations_.back();
}

void NvnBufferCache::sync_dirty_ranges(VkCommandBuffer cmd) {
    (void)cmd;
    for (auto &alloc : allocations_) {
        if (alloc.is_dirty) {
            // Read guest memory and sync to host buffer
            if (alloc.mapped_host && alloc.guest_addr) {
                std::memcpy(alloc.mapped_host, reinterpret_cast<const void *>(static_cast<uintptr_t>(alloc.guest_addr)), alloc.size);
            }
            alloc.is_dirty = false;
        }
    }
}

void NvnBufferCache::invalidate_range(uint64_t guest_addr, size_t size) {
    for (auto &alloc : allocations_) {
        if (guest_addr >= alloc.guest_addr && (guest_addr + size) <= (alloc.guest_addr + alloc.size)) {
            alloc.is_dirty = true;
        }
    }
}

} // namespace totk::graphics
