#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include "volk.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace totk::graphics {

struct UniformSnapshot {
    uint32_t stage = 0;
    uint32_t slot = 0;
    uint32_t offset = 0;
    uint32_t size = 0;
    std::vector<uint8_t> data;
};

class NvnUniformSnapshotRing {
public:
    explicit NvnUniformSnapshotRing(size_t ring_size = 4 * 1024 * 1024); // 4 MiB ring
    ~NvnUniformSnapshotRing();

    void initialize(VkDevice device);
    uint32_t push_snapshot(uint32_t stage, uint32_t slot, const void *data, uint32_t size);
    void reset();

    size_t get_used_bytes() const { return cursor_; }
    size_t get_snapshot_count() const { return snapshots_.size(); }
    const std::vector<UniformSnapshot> &get_snapshots() const { return snapshots_; }

private:
    size_t ring_size_ = 0;
    size_t cursor_ = 0;
    VkDevice device_ = VK_NULL_HANDLE;
    VkBuffer ring_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory ring_memory_ = VK_NULL_HANDLE;
    uint8_t *mapped_ptr_ = nullptr;
    std::vector<UniformSnapshot> snapshots_;
};

} // namespace totk::graphics
