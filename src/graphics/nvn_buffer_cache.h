#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include "volk.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace totk::graphics {

struct BufferAllocation {
    uint64_t guest_addr = 0;
    size_t size = 0;
    VkBuffer vk_buffer = VK_NULL_HANDLE;
    VkDeviceMemory vk_memory = VK_NULL_HANDLE;
    void *mapped_host = nullptr;
    bool is_dirty = false;
};

class NvnBufferCache {
public:
    explicit NvnBufferCache(VkDevice device = VK_NULL_HANDLE);
    ~NvnBufferCache();

    void set_device(VkDevice device, VkPhysicalDevice physical_device);

    BufferAllocation *register_guest_buffer(uint64_t guest_addr, size_t size);
    void sync_dirty_ranges(VkCommandBuffer cmd);
    void invalidate_range(uint64_t guest_addr, size_t size);

    size_t get_tracked_buffer_count() const { return allocations_.size(); }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    std::vector<BufferAllocation> allocations_;
};

} // namespace totk::graphics
