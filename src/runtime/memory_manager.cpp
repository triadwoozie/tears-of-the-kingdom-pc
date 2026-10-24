#include "memory_manager.h"
#include <cstdio>
#include <cstring>

#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace totk::runtime {

MemoryManager &MemoryManager::instance() {
    static MemoryManager mgr;
    return mgr;
}

MemoryManager::MemoryManager() = default;

MemoryManager::~MemoryManager() {
    shutdown();
}

bool MemoryManager::initialize() {
    if (guest_base_) return true;

#if defined(__linux__)
    void *ptr = mmap(reinterpret_cast<void *>(GUEST_MEMORY_BASE), GUEST_MEMORY_SIZE,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);

    if (ptr == MAP_FAILED || ptr != reinterpret_cast<void *>(GUEST_MEMORY_BASE)) {
        // Fallback: try standard MAP_FIXED
        ptr = mmap(reinterpret_cast<void *>(GUEST_MEMORY_BASE), GUEST_MEMORY_SIZE,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
        if (ptr == MAP_FAILED) {
            std::fprintf(stderr, "[MemoryManager] Failed to reserve 39-bit guest window at 0x%016llx\n",
                         static_cast<unsigned long long>(GUEST_MEMORY_BASE));
            return false;
        }
    }
    guest_base_ = ptr;
#elif defined(_WIN32)
    void *ptr = VirtualAlloc(reinterpret_cast<void *>(GUEST_MEMORY_BASE), GUEST_MEMORY_SIZE,
                             MEM_RESERVE | MEM_WRITE_WATCH, PAGE_READWRITE);
    if (!ptr) {
        std::fprintf(stderr, "[MemoryManager] VirtualAlloc failed to reserve guest window.\n");
        return false;
    }
    guest_base_ = ptr;
#endif

    std::printf("[MemoryManager] Reserved 39-bit guest memory window: [0x%016llx - 0x%016llx] (%.1f GiB)\n",
                static_cast<unsigned long long>(GUEST_MEMORY_BASE),
                static_cast<unsigned long long>(GUEST_MEMORY_LIMIT),
                GUEST_MEMORY_SIZE / (1024.0 * 1024.0 * 1024.0));
    return true;
}

void MemoryManager::shutdown() {
    if (!guest_base_) return;

#if defined(__linux__)
    munmap(guest_base_, GUEST_MEMORY_SIZE);
    if (uffd_fd_ >= 0) close(uffd_fd_);
    if (pagemap_fd_ >= 0) close(pagemap_fd_);
#elif defined(_WIN32)
    VirtualFree(guest_base_, 0, MEM_RELEASE);
#endif

    guest_base_ = nullptr;
}

bool MemoryManager::commit(uint64_t addr, size_t size) {
    if (!is_in_guest_range(addr)) return false;

#if defined(_WIN32)
    return VirtualAlloc(reinterpret_cast<void *>(addr), size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
#else
    // On Linux with MAP_NORESERVE, pages are on-demand committed by the kernel upon first access
    (void)addr; (void)size;
    return true;
#endif
}

bool MemoryManager::enable_write_tracking() {
    write_tracking_enabled_ = true;
    return true;
}

int MemoryManager::get_written_pages(uint64_t addr, size_t size, uint64_t *pages, int max_pages) {
    if (!write_tracking_enabled_ || !is_in_guest_range(addr)) return -1;

#if defined(_WIN32)
    ULONG_PTR count = max_pages;
    ULONG page_size = 4096;
    if (GetWriteWatch(WRITE_WATCH_FLAG_RESET, reinterpret_cast<void *>(addr), size,
                      reinterpret_cast<void **>(pages), &count, &page_size) == 0) {
        return static_cast<int>(count);
    }
    return -1;
#else
    (void)addr; (void)size; (void)pages; (void)max_pages;
    return 0;
#endif
}

} // namespace totk::runtime
