#pragma once

#include <cstddef>
#include <cstdint>

namespace totk::runtime {

constexpr uint64_t GUEST_MEMORY_BASE = 0x7000000000ull;
constexpr uint64_t GUEST_MEMORY_LIMIT = 0x7320000000ull;
constexpr size_t GUEST_MEMORY_SIZE = GUEST_MEMORY_LIMIT - GUEST_MEMORY_BASE; // 12.5 GiB

class MemoryManager {
public:
    static MemoryManager &instance();

    bool initialize();
    void shutdown();

    void *get_guest_base() const { return guest_base_; }
    size_t get_guest_size() const { return GUEST_MEMORY_SIZE; }

    bool is_in_guest_range(uint64_t addr) const {
        return addr >= GUEST_MEMORY_BASE && addr < GUEST_MEMORY_LIMIT;
    }

    // Page write tracking
    bool enable_write_tracking();
    int get_written_pages(uint64_t addr, size_t size, uint64_t *pages, int max_pages);

    // Commit physical memory to guest range
    bool commit(uint64_t addr, size_t size);

private:
    MemoryManager();
    ~MemoryManager();

    void *guest_base_ = nullptr;
    bool write_tracking_enabled_ = false;
    int uffd_fd_ = -1;
    int pagemap_fd_ = -1;
};

} // namespace totk::runtime
