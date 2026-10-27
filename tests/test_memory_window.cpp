#include "memory_manager.h"
#include <cassert>
#include <cstdio>

int main() {
    std::printf("[Test] Testing 39-bit Virtual Memory Window...\n");

    auto &mem = totk::runtime::MemoryManager::instance();
    bool init_ok = mem.initialize();
    assert(init_ok);
    assert(mem.get_guest_base() != nullptr);

    // Verify boundary range
    assert(mem.is_in_guest_range(totk::runtime::GUEST_MEMORY_BASE));
    assert(mem.is_in_guest_range(totk::runtime::GUEST_MEMORY_LIMIT - 1));
    assert(!mem.is_in_guest_range(totk::runtime::GUEST_MEMORY_BASE - 1));
    assert(!mem.is_in_guest_range(totk::runtime::GUEST_MEMORY_LIMIT));

#if defined(__linux__)
    assert(mem.has_underflow_guard());
    volatile uint64_t *guard_ptr = reinterpret_cast<volatile uint64_t *>(
        totk::runtime::GUEST_MEMORY_BASE - 0x20);
    assert(*guard_ptr == 0);
    *guard_ptr = 0x123456789abcdef0ULL;
    assert(*guard_ptr == 0x123456789abcdef0ULL);
#endif

    // Test committing a page inside the guest range
    uint64_t test_addr = totk::runtime::GUEST_MEMORY_BASE + 0x10000;
    bool commit_ok = mem.commit(test_addr, 4096);
    assert(commit_ok);

    // Test write and read through direct pointer
    volatile uint32_t *ptr = reinterpret_cast<volatile uint32_t *>(static_cast<uintptr_t>(test_addr));
    *ptr = 0x12345678;
    assert(*ptr == 0x12345678);

    std::printf("[Test] 39-bit guest memory window verified successfully!\n");
    return 0;
}
