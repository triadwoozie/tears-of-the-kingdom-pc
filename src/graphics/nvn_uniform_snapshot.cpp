#include "nvn_uniform_snapshot.h"
#include <cstring>

namespace totk::graphics {

NvnUniformSnapshotRing::NvnUniformSnapshotRing(size_t ring_size)
    : ring_size_(ring_size) {}

NvnUniformSnapshotRing::~NvnUniformSnapshotRing() {
    if (device_ != VK_NULL_HANDLE) {
        if (mapped_ptr_) vkUnmapMemory(device_, ring_memory_);
        if (ring_buffer_) vkDestroyBuffer(device_, ring_buffer_, nullptr);
        if (ring_memory_) vkFreeMemory(device_, ring_memory_, nullptr);
    }
}

void NvnUniformSnapshotRing::initialize(VkDevice device) {
    device_ = device;
    if (device_ == VK_NULL_HANDLE) return;

    VkBufferCreateInfo buf_info{};
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size = ring_size_;
    buf_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(device_, &buf_info, nullptr, &ring_buffer_);

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(device_, ring_buffer_, &mem_reqs);

    VkMemoryAllocateInfo mem_alloc{};
    mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mem_alloc.allocationSize = mem_reqs.size;
    mem_alloc.memoryTypeIndex = 0; // Host visible memory type

    vkAllocateMemory(device_, &mem_alloc, nullptr, &ring_memory_);
    vkBindBufferMemory(device_, ring_buffer_, ring_memory_, 0);
    vkMapMemory(device_, ring_memory_, 0, ring_size_, 0, reinterpret_cast<void **>(&mapped_ptr_));
}

uint32_t NvnUniformSnapshotRing::push_snapshot(uint32_t stage, uint32_t slot, const void *data, uint32_t size) {
    // 256-byte alignment required for dynamic uniform buffer offsets in Vulkan
    size_t aligned_offset = (cursor_ + 255) & ~255;
    if (aligned_offset + size > ring_size_) {
        aligned_offset = 0; // Wrap ring
    }

    if (mapped_ptr_ && data && size > 0) {
        std::memcpy(mapped_ptr_ + aligned_offset, data, size);
    }

    UniformSnapshot snap;
    snap.stage = stage;
    snap.slot = slot;
    snap.offset = static_cast<uint32_t>(aligned_offset);
    snap.size = size;
    if (data && size > 0) {
        snap.data.resize(size);
        std::memcpy(snap.data.data(), data, size);
    }
    snapshots_.push_back(snap);

    cursor_ = aligned_offset + size;
    return static_cast<uint32_t>(aligned_offset);
}

void NvnUniformSnapshotRing::reset() {
    cursor_ = 0;
    snapshots_.clear();
}

} // namespace totk::graphics
