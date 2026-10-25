#include "hlr_sead.h"
#include "recompiler/dispatch.h"
#include "recompiler/arm64_ctx.h"
#include "runtime/memory_manager.h"
#include <algorithm>
#include <cmath>
#include <immintrin.h>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <unordered_set>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <mutex>

namespace totk::hlr {

static std::atomic<uint64_t> s_hlr_bump_heap{0x7222000000ULL};

static bool is_mapped_guest_range(totk::runtime::MemoryManager &memory,
                                  uint64_t address, size_t size) {
    return memory.get_guest_base() != nullptr &&
           address >= totk::runtime::GUEST_MEMORY_BASE &&
           address <= totk::runtime::GUEST_MEMORY_LIMIT &&
           size <= totk::runtime::GUEST_MEMORY_LIMIT - address &&
           memory.commit(address, size);
}

static uint64_t hlr_bump_alloc(uint64_t size, uint64_t align) {
    if (size == 0) size = 0x40;
    if (align == 0) align = 0x10;

    uint64_t current;
    uint64_t next;
    do {
        current = s_hlr_bump_heap.load(std::memory_order_relaxed);
        uint64_t aligned = (current + (align - 1)) & ~(align - 1);
        next = aligned + ((size + 0xfULL) & ~0xfULL);
    } while (!s_hlr_bump_heap.compare_exchange_weak(current, next, std::memory_order_relaxed));

    uint64_t res = (current + (align - 1)) & ~(align - 1);
    totk::runtime::MemoryManager::instance().commit(res, (size + 0xfffULL) & ~0xfffULL);
    std::memset(reinterpret_cast<void *>(res), 0, (size < 256 ? size : 256));
    return res;
}

static uint64_t get_dummy_sead_resource() {
    static const uint64_t s_dummy_resource = [] {
        uint64_t resource = hlr_bump_alloc(0x200, 0x20);
        uint64_t vtbl = hlr_bump_alloc(0x100, 0x10);
        for (int i = 0; i < 32; ++i) {
            *reinterpret_cast<uint64_t *>(vtbl + i * 8) = 0x7104214ea8ULL;
        }
        *reinterpret_cast<uint64_t *>(resource) = vtbl;
        *reinterpret_cast<uint64_t *>(resource + 0x28) = hlr_bump_alloc(0x1000, 0x10);
        *reinterpret_cast<uint32_t *>(resource + 0x30) = 0x1000;
        *reinterpret_cast<uint8_t *>(resource + 0x58) = 1;
        return resource;
    }();
    return s_dummy_resource;
}

static uint64_t get_safe_container_node() {
    static uint64_t s_node = 0;
    if (s_node == 0) {
        s_node = hlr_bump_alloc(0x1000, 0x10);
        *reinterpret_cast<uint8_t *>(s_node + 0x64) = 1;
        *reinterpret_cast<uint8_t *>(s_node + 0x65) = 1;
        *reinterpret_cast<uint8_t *>(s_node + 0x66) = 1;
        *reinterpret_cast<uint8_t *>(s_node + 0x67) = 1;
        *reinterpret_cast<uint8_t *>(s_node + 0xa4) = 1;
        *reinterpret_cast<uint64_t *>(s_node + 0x30) = s_node + 0x100;
    }
    return s_node;
}

static uint64_t get_default_heap() {
    static uint64_t s_default_heap = 0;
    if (s_default_heap == 0) {
        s_default_heap = hlr_bump_alloc(0x200, 0x40);
        uint64_t vtbl = hlr_bump_alloc(0x400, 0x20);
        for (int i = 0; i < 0x400 / 8; ++i) {
            *reinterpret_cast<uint64_t *>(vtbl + i * 8) = 0x710406a920ULL; // safe methods
        }
        *reinterpret_cast<uint64_t *>(s_default_heap) = vtbl;
    }
    return s_default_heap;
}

static void hlr_alloc_hook(totk::recompiler::Arm64Context *c) {
    uint64_t sz = c->x[0];
    uint64_t align = c->x[2];
    if (c->x[0] >= 0x7000000000ULL || c->x[0] == 0) {
        // x0 is a pointer (e.g. this pointer to sead::Heap, possibly NULL)
        sz = (c->x[1] != 0) ? c->x[1] : 8;
        align = (c->x[2] != 0 && c->x[2] <= 0x1000) ? c->x[2] : 8;
    } else {
        if (align == 0 || align > 0x1000) {
            align = 8;
        }
    }
    if (sz == 0) sz = 8;
    if (align == 0) align = 8;
    if (sz > 0x40000000ULL) { // 1 GiB safety clamp
        sz = 0x1000;
    }
    c->x[0] = hlr_bump_alloc(sz, align);
    if (c->x[30] == 0x71048f0e0cULL) {
        uint64_t c12 = (c->x[0] + 7) & ~7;
        if (c12 >= 0x7000000000ULL && c12 < 0x7300000000ULL) {
            *reinterpret_cast<uint64_t *>(c12 + 0x18) = hlr_bump_alloc(0x40000, 0x40);
            *reinterpret_cast<uint32_t *>(c12 + 0x40) = 0x1000;
        }
    }
    c->pc = c->x[30];
}

void register_sead_hlr_hooks() {
    using totk::recompiler::Arm64Context;

    totk::recompiler::register_recompiled_func(0x710415fb70ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    totk::recompiler::register_recompiled_func(0x71049b7e80ULL, [](Arm64Context *c) {
        if (!is_mapped_guest_range(totk::runtime::MemoryManager::instance(),
                                   c->x[0], 0xa6)) {
            c->x[0] = 0;
            c->pc = c->x[30];
            return;
        }

        uint8_t flag = 0;
        std::memcpy(&flag, reinterpret_cast<const void *>(c->x[0] + 0xa5),
                    sizeof(flag));
        if (flag & 1) {
            uint64_t ret = 0;
            std::memcpy(&ret, reinterpret_cast<const void *>(c->x[0] + 0x98),
                        sizeof(ret));
            c->x[0] = ret;
        } else {
            c->x[0] = 0;
        }
        c->pc = c->x[30];
    });

    totk::recompiler::register_recompiled_func(0x71049c8eccULL, [](Arm64Context *c) {
        if (!is_mapped_guest_range(totk::runtime::MemoryManager::instance(),
                                   c->x[0], 0xa9)) {
            c->x[0] = 0;
            c->pc = c->x[30];
            return;
        }

        uint8_t flag = 0;
        std::memcpy(&flag, reinterpret_cast<const void *>(c->x[0] + 0xa8),
                    sizeof(flag));
        if (flag & 1) {
            c->x[0] = c->x[0] + 0x68;
        } else {
            c->x[0] = 0;
        }
        c->pc = c->x[30];
    });

    totk::recompiler::register_recompiled_func(0x71049ba158ULL, [](Arm64Context *c) {
        if (!is_mapped_guest_range(totk::runtime::MemoryManager::instance(),
                                   c->x[0], 0xa7)) {
            c->x[0] = 0;
            c->pc = c->x[30];
            return;
        }

        uint8_t flag = 0;
        std::memcpy(&flag, reinterpret_cast<const void *>(c->x[0] + 0xa6),
                    sizeof(flag));
        if (flag & 1) {
            c->x[0] = c->x[0] + 0x38;
        } else {
            c->x[0] = 0;
        }
        c->pc = c->x[30];
    });

    totk::recompiler::register_recompiled_func(0x7104d23fe4ULL, [](Arm64Context *c) {
        if (!is_mapped_guest_range(totk::runtime::MemoryManager::instance(),
                                   c->x[0], 0xaa)) {
            c->x[0] = 0;
            c->pc = c->x[30];
            return;
        }

        uint8_t flag = 0;
        std::memcpy(&flag, reinterpret_cast<const void *>(c->x[0] + 0xa9),
                    sizeof(flag));
        if (flag & 1) {
            c->x[0] = c->x[0] + 0x60;
        } else {
            c->x[0] = 0;
        }
        c->pc = c->x[30];
    });

    totk::recompiler::register_recompiled_func(0x7104bfed4cULL, [](Arm64Context *c) {
        if (!is_mapped_guest_range(totk::runtime::MemoryManager::instance(),
                                   c->x[0], 0x61)) {
            c->x[0] = 0;
            c->pc = c->x[30];
            return;
        }

        uint8_t flag = 0;
        std::memcpy(&flag, reinterpret_cast<const void *>(c->x[0] + 0x60),
                    sizeof(flag));
        if (flag & 1) {
            c->x[0] = c->x[0] + 0x30;
        } else {
            c->x[0] = 0;
        }
        c->pc = c->x[30];
    });

    totk::recompiler::register_recompiled_func(0x7104bfedb0ULL, [](Arm64Context *c) {
        if (!is_mapped_guest_range(totk::runtime::MemoryManager::instance(),
                                   c->x[0], 0x62)) {
            c->x[0] = 0;
            c->pc = c->x[30];
            return;
        }

        uint8_t flag = 0;
        std::memcpy(&flag, reinterpret_cast<const void *>(c->x[0] + 0x61),
                    sizeof(flag));
        if (flag & 1) {
            c->x[0] = c->x[0] + 0x58;
        } else {
            c->x[0] = 0;
        }
        c->pc = c->x[30];
    });

    // Avoid the original indirect calls, repairing missing sead singleton
    // instances or vtables with no-op dummy objects when guest memory is ready.
    totk::recompiler::register_recompiled_func(0x7104531f4cULL, [](Arm64Context *c) {
        constexpr uint64_t guest_base = totk::runtime::GUEST_MEMORY_BASE;
        constexpr uint64_t guest_limit = totk::runtime::GUEST_MEMORY_LIMIT;
        constexpr uint64_t dummy_method = 0x7104531f4cULL;
        constexpr uint64_t singleton_slots[] = {
            0x7107965000ULL + 0x358ULL,
            0x710795e000ULL + 0xc38ULL,
        };
        static std::mutex singleton_mutex;
        std::lock_guard<std::mutex> lock(singleton_mutex);

        auto valid_guest_range = [](uint64_t address, uint64_t size) {
            return address >= guest_base && address <= guest_limit &&
                   size <= guest_limit - address;
        };

        auto &memory = totk::runtime::MemoryManager::instance();
        if (memory.get_guest_base() != nullptr) {
            for (uint64_t slot : singleton_slots) {
                if (!valid_guest_range(slot, sizeof(uint64_t)) ||
                    !memory.commit(slot, sizeof(uint64_t))) {
                    continue;
                }

                uint64_t instance = *reinterpret_cast<const uint64_t *>(slot);
                bool needs_dummy =
                    instance == 0 || !valid_guest_range(instance, sizeof(uint64_t)) ||
                    !memory.commit(instance, sizeof(uint64_t));
                if (!needs_dummy) {
                    const uint64_t vtable =
                        *reinterpret_cast<const uint64_t *>(instance);
                    needs_dummy =
                        vtable == 0 || !valid_guest_range(vtable, sizeof(uint64_t)) ||
                        !memory.commit(vtable, sizeof(uint64_t));
                }

                if (needs_dummy) {
                    const uint64_t dummy_instance = hlr_bump_alloc(0x100, 0x10);
                    const uint64_t dummy_vtable = hlr_bump_alloc(0x400, 0x20);
                    for (uint64_t offset = 0; offset < 0x400; offset += sizeof(uint64_t)) {
                        *reinterpret_cast<uint64_t *>(dummy_vtable + offset) = dummy_method;
                    }
                    *reinterpret_cast<uint64_t *>(dummy_instance) = dummy_vtable;
                    *reinterpret_cast<uint64_t *>(slot) = dummy_instance;
                }
            }
        }

        c->x[0] = 0;
        c->pc = c->x[30];
    });

    static totk::recompiler::RecompiledFunc s_orig_710421e70c =
        totk::recompiler::lookup_recompiled_func(0x710421e70cULL);
    (void)s_orig_710421e70c;
    totk::recompiler::register_recompiled_func(0x710421e70cULL, [](Arm64Context *c) {
        uint32_t total = 0;
        uint64_t curr = c->x[0];
        int depth = 0;
        while (curr >= 0x7000000000ULL && curr < 0x7300000000ULL - 0x20 && depth < 64) {
            total += *reinterpret_cast<uint32_t *>(curr + 0x8);
            curr = *reinterpret_cast<uint64_t *>(curr + 0x18);
            depth++;
        }
        c->x[0] = total;
        c->pc = c->x[30];
    });

    static totk::recompiler::RecompiledFunc s_orig_71043792d0 =
        totk::recompiler::lookup_recompiled_func(0x71043792d0ULL);
    totk::recompiler::register_recompiled_func(0x71043792d0ULL, [](Arm64Context *c) {
        constexpr uint64_t guest_base = 0x7000000000ULL;
        constexpr uint64_t guest_limit = 0x7300000000ULL;

        if (c->x[0] < guest_base || c->x[0] >= guest_limit) {
            c->x[0] = 0xffffffff;
            c->pc = c->x[30];
            return;
        }

        const uint64_t obj22 = *reinterpret_cast<const uint64_t *>(c->x[0] + 0xd8);
        if (obj22 < guest_base || obj22 >= guest_limit - 0x190) {
            c->x[0] = 0xffffffff;
            c->pc = c->x[30];
            return;
        }

        uint64_t *p_sub8 = reinterpret_cast<uint64_t *>(obj22 + 0x188);
        if (*p_sub8 < guest_base || *p_sub8 >= guest_limit) {
            c->x[0] = 0xffffffff;
            c->pc = c->x[30];
            return;
        }

        if (s_orig_71043792d0) {
            s_orig_71043792d0(c);
        } else {
            c->x[0] = 0xffffffff;
            c->pc = c->x[30];
        }
    });

    static totk::recompiler::RecompiledFunc s_orig_71043d4a38 =
        totk::recompiler::lookup_recompiled_func(0x71043d4a38ULL);
    totk::recompiler::register_recompiled_func(0x71043d4a38ULL, [](Arm64Context *c) {
        constexpr uint64_t guest_base = 0x7000000000ULL;
        constexpr uint64_t guest_limit = 0x7300000000ULL;

        if (c->x[0] < guest_base || c->x[0] >= guest_limit) {
            c->pc = c->x[30];
            return;
        }

        uint64_t target = (c->x[2] != 0) ? c->x[2] : c->x[7];
        if (target < guest_base || target >= guest_limit - 0x100) {
            target = hlr_bump_alloc(0x200, 0x10);
            if (c->x[2] != 0) c->x[2] = target;
            else c->x[7] = target;
        }

        *reinterpret_cast<uint64_t *>(target + 104) = 0;
        *reinterpret_cast<uint32_t *>(target + 104 + 0x14) = 0;
        *reinterpret_cast<uint32_t *>(target + 0x78) = 0;
        *reinterpret_cast<uint32_t *>(target + 0xa8) = 1;
        *reinterpret_cast<uint32_t *>(target + 0x88) = 0;

        if (s_orig_71043d4a38) {
            s_orig_71043d4a38(c);
        } else {
            c->pc = c->x[30];
        }
    });

    // Guard f_71046f4818 against invalid object pointers and offsets.
    totk::recompiler::register_recompiled_func(0x71046f4818ULL, [](Arm64Context *c) {
        constexpr uint64_t guest_base = 0x7000000000ULL;
        constexpr uint64_t guest_limit = 0x7300000000ULL;
        auto valid_range = [](uint64_t addr, uint64_t size) {
            return addr >= guest_base && addr <= guest_limit &&
                   size <= guest_limit - addr;
        };

        const uint64_t table_address =
            0x7106cf1d58ULL + ((c->x[1] & 0xffffffffULL) << 3);
        if (!valid_range(c->x[0], 1) || !valid_range(table_address, sizeof(uint64_t))) {
            c->x[0] = 0;
            c->pc = c->x[30];
            return;
        }

        const uint64_t offset = *reinterpret_cast<const uint64_t *>(table_address);
        if (offset > UINT64_MAX - c->x[0]) {
            c->x[0] = 0;
            c->pc = c->x[30];
            return;
        }

        const uint64_t value_address = c->x[0] + offset;
        if (!valid_range(value_address, sizeof(uint64_t))) {
            c->x[0] = 0;
        } else {
            const uint64_t value = *reinterpret_cast<const uint64_t *>(value_address);
            c->x[0] = valid_range(value, 1) ? value : 0;
        }
        c->pc = c->x[30];
    });

    static totk::recompiler::RecompiledFunc s_orig_710472fbd0 =
        totk::recompiler::lookup_recompiled_func(0x710472fbd0ULL);
    totk::recompiler::register_recompiled_func(0x710472fbd0ULL, [](Arm64Context *c) {
        if (c->x[0] < 0x7000000000ULL || c->x[0] >= 0x7300000000ULL) {
            c->x[0] = hlr_bump_alloc(0x1000, 0x10);
            if (c->x[2] >= 0x7000000000ULL && c->x[2] < 0x7300000000ULL) {
                *reinterpret_cast<uint64_t *>(c->x[2] + 0x68) = c->x[0];
            }
        }
        if (c->x[2] < 0x7000000000ULL || c->x[2] >= 0x7300000000ULL) {
            c->x[2] = hlr_bump_alloc(0x200, 0x10);
        }
        if (s_orig_710472fbd0) s_orig_710472fbd0(c);
        else c->pc = c->x[30];
    });

    static totk::recompiler::RecompiledFunc s_orig_71068ff3f0 =
        totk::recompiler::lookup_recompiled_func(0x71068ff3f0ULL);
    totk::recompiler::register_recompiled_func(0x71068ff3f0ULL, [](Arm64Context *c) {
        if (c->x[2] < 0x7000000000ULL || c->x[2] >= 0x7300000000ULL) {
            uint64_t sz = (c->x[0] > 0 && c->x[0] < 0x10000000ULL) ? c->x[0] : 0x1000;
            c->x[0] = hlr_bump_alloc(sz, 16);
            c->pc = c->x[30];
            return;
        }
        if (s_orig_71068ff3f0) s_orig_71068ff3f0(c);
        else {
            uint64_t sz = (c->x[0] > 0 && c->x[0] < 0x10000000ULL) ? c->x[0] : 0x1000;
            c->x[0] = hlr_bump_alloc(sz, 16);
            c->pc = c->x[30];
        }
    });

    static totk::recompiler::RecompiledFunc s_orig_7104338564 =
        totk::recompiler::lookup_recompiled_func(0x7104338564ULL);
    totk::recompiler::register_recompiled_func(0x7104338564ULL, [](Arm64Context *c) {
        constexpr uint64_t guest_base = 0x7000000000ULL;
        constexpr uint64_t guest_limit = 0x7300000000ULL;
        auto fail = [c]() {
            c->x[0] = 0;
            c->pc = c->x[30];
        };

        const uint64_t node = c->x[0];
        if (node < guest_base || node >= guest_limit) {
            fail();
            return;
        }
        if (node > guest_limit - 0x18) {
            fail();
            return;
        }

        const uint64_t vtable = *reinterpret_cast<const uint64_t *>(node);
        if (vtable < guest_base || vtable >= guest_limit - 0x100) {
            fail();
            return;
        }

        uint64_t *const fn_slot = reinterpret_cast<uint64_t *>(vtable + 0x80);
        if (*fn_slot < 0x7100000000ULL || *fn_slot >= 0x7110000000ULL) {
            *fn_slot = 0x71068eb97cULL;
        }

        const uint64_t next = *reinterpret_cast<const uint64_t *>(node + 0x10);
        static thread_local std::unordered_set<uint64_t> visited_nodes;
        static thread_local size_t recursion_depth = 0;
        if (recursion_depth >= 32 || visited_nodes.contains(node)) {
            fail();
            return;
        }

        struct RecursionGuard {
            std::unordered_set<uint64_t> &visited;
            size_t &depth;
            uint64_t node;

            ~RecursionGuard() {
                visited.erase(node);
                --depth;
            }
        };

        visited_nodes.insert(node);
        ++recursion_depth;
        RecursionGuard recursion_guard{visited_nodes, recursion_depth, node};
        if (next != 0 && visited_nodes.contains(next)) {
            fail();
            return;
        }

        if (s_orig_7104338564) {
            s_orig_7104338564(c);
        } else {
            c->pc = c->x[30];
        }
    });

    // Hook universal sead::Heap::alloc (0x710406a920)
    totk::recompiler::register_recompiled_func(0x710406a920ULL, hlr_alloc_hook);

    // Hook sead::Heap::alloc virtual dispatcher (0x71042c99e4)
    totk::recompiler::register_recompiled_func(0x71042c99e4ULL, hlr_alloc_hook);

    // Hook sead::ExpHeap::alloc (0x7104064db0)
    totk::recompiler::register_recompiled_func(0x7104064db0ULL, hlr_alloc_hook);

    // Hook sead::FrameHeap::alloc (0x710406f110)
    totk::recompiler::register_recompiled_func(0x710406f110ULL, hlr_alloc_hook);

    // Hook operator new(size, heap, align) (0x7104412550)
    totk::recompiler::register_recompiled_func(0x7104412550ULL, hlr_alloc_hook);

    // Hook subheap node allocators
    totk::recompiler::register_recompiled_func(0x71045709d8ULL, [](Arm64Context *c) {
        c->x[0] = hlr_bump_alloc(0x200, 0x10);
        c->pc = c->x[30];
    });

    totk::recompiler::register_recompiled_func(0x7104570b9cULL, [](Arm64Context *c) {
        c->x[0] = hlr_bump_alloc(0x200, 0x10);
        c->pc = c->x[30];
    });

    // Hook nnmem_nlib_debug_break (0x710c129050)
    totk::recompiler::register_recompiled_func(0x710c129050ULL, [](Arm64Context *c) {
        static std::unordered_set<uint64_t> s_warned;
        if (s_warned.insert(c->x[30]).second) {
            std::printf("  [SDK DebugBreak] Triggered from LR=0x%llx (X0=0x%llx, X1=0x%llx)\n",
                        static_cast<unsigned long long>(c->x[30]),
                        static_cast<unsigned long long>(c->x[0]),
                        static_cast<unsigned long long>(c->x[1]));
            std::fflush(stdout);
        }
        c->pc = c->x[30];
        c->x[0] = 0;
    });

    // Hook the internal audio/IPC accessor, which returns a status code.
    totk::recompiler::register_recompiled_func(0x710c1f5fd0ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
        return;
    });

    // Hook the audio renderer device descriptor query (0x710c1f6040).
    totk::recompiler::register_recompiled_func(0x710c1f6040ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
        return;
    });

    // Hook the remaining audio renderer query helpers.
    totk::recompiler::register_recompiled_func(0x710c1f6b50ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
        return;
    });

    totk::recompiler::register_recompiled_func(0x710c1f61a0ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
        return;
    });

    totk::recompiler::register_recompiled_func(0x710c1f6c10ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
        return;
    });

    totk::recompiler::register_recompiled_func(0x710c1f6c70ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
        return;
    });

    totk::recompiler::register_recompiled_func(0x710c1f5f60ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
        return;
    });

    totk::recompiler::register_recompiled_func(0x710c1f69d0ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
        return;
    });

    totk::recompiler::register_recompiled_func(0x710c1f6a30ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
        return;
    });

    totk::recompiler::register_recompiled_func(0x710c1f6a90ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
        return;
    });

    totk::recompiler::register_recompiled_func(0x710c1f6ce0ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
        return;
    });

    // Hook nn::oe::Initialize (0x710c407930)
    totk::recompiler::register_recompiled_func(0x710c407930ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook nn::oe::InitializeApplet (0x710c408200)
    totk::recompiler::register_recompiled_func(0x710c408200ULL, [](Arm64Context *c) {
        std::printf("  [HLR Hook] nn::oe::InitializeApplet() -> SUCCESS\n");
        std::fflush(stdout);
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook nn::oe::GetCurrentFocusState (0x710c408890)
    totk::recompiler::register_recompiled_func(0x710c408890ULL, [](Arm64Context *c) {
        c->x[0] = 1; // FocusState::InFocus
        c->pc = c->x[30];
    });

    // Hook nn::oe::GetOperationMode (0x710c4089a0)
    totk::recompiler::register_recompiled_func(0x710c4089a0ULL, [](Arm64Context *c) {
        c->x[0] = 1; // OperationMode::Docked
        c->pc = c->x[30];
    });

    // Hook nn::oe::GetPerformanceMode (0x710c408b50)
    totk::recompiler::register_recompiled_func(0x710c408b50ULL, [](Arm64Context *c) {
        c->x[0] = 1; // PerformanceMode::Boost
        c->pc = c->x[30];
    });

    // Hook nn::oe::GetDesiredLanguage (0x710c408e30)
    totk::recompiler::register_recompiled_func(0x710c408e30ULL, [](Arm64Context *c) {
        c->x[0] = 1; // English
        c->pc = c->x[30];
    });

    // Hook nn::oe::SetPerformanceConfiguration (0x710c40a080 and 0x710c40a1b0)
    totk::recompiler::register_recompiled_func(0x710c40a080ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
    });
    totk::recompiler::register_recompiled_func(0x710c40a1b0ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook nn::oe::SetFocusHandlingMode (0x710c4086a0)
    totk::recompiler::register_recompiled_func(0x710c4086a0ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook nn::oe::SetFocusHandlingModeForDebug (0x710c4087f0)
    totk::recompiler::register_recompiled_func(0x710c4087f0ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook nn::oe::SetResumeNotificationEnabled (0x710c4088e0)
    totk::recompiler::register_recompiled_func(0x710c4088e0ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook nn::oe::SetOperationModeChangedNotificationEnabled (0x710c408920)
    totk::recompiler::register_recompiled_func(0x710c408920ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook nn::oe::SetPerformanceModeChangedNotificationEnabled (0x710c408960)
    totk::recompiler::register_recompiled_func(0x710c408960ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook nn::oe::TryPopNotificationMessage (0x710c408420)
    totk::recompiler::register_recompiled_func(0x710c408420ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook nn::applet::SetCpuBoostMode (0x710c1eb850)
    totk::recompiler::register_recompiled_func(0x710c1eb850ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook nn::applet::CancelCpuBoostMode (0x710c1eb8a0)
    totk::recompiler::register_recompiled_func(0x710c1eb8a0ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook nn::applet::GetAppletResourceUserId (0x710c1ea6b0)
    totk::recompiler::register_recompiled_func(0x710c1ea6b0ULL, [](Arm64Context *c) {
        c->x[0] = 1;
        c->pc = c->x[30];
    });

    // Hook nn::applet::TryPopFromApplicationParameterChannel (0x710c1ead70)
    totk::recompiler::register_recompiled_func(0x710c1ead70ULL, [](Arm64Context *c) {
        c->x[0] = 0; // false: no launch parameter
        c->pc = c->x[30];
    });

    // Hook nn::applet::GetPreviousProgramIndex (0x710c1eb180)
    totk::recompiler::register_recompiled_func(0x710c1eb180ULL, [](Arm64Context *c) {
        c->x[0] = 0; // program index 0
        c->pc = c->x[30];
    });

    // Hook nn::oe::GetPreviousProgramIndex (0x710c40a780)
    totk::recompiler::register_recompiled_func(0x710c40a780ULL, [](Arm64Context *c) {
        c->x[0] = 0; // program index 0
        c->pc = c->x[30];
    });

    static const uint8_t s_default_uid[16] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10
    };

    // Hook nn::account::Initialize (0x710c1b0fb0)
    totk::recompiler::register_recompiled_func(0x710c1b0fb0ULL, [](Arm64Context *c) {
        c->x[0] = 0; // ResultSuccess
        c->pc = c->x[30];
    });

    // Hook nn::account::ListQualifiedUsers (0x710c1b15a0)
    totk::recompiler::register_recompiled_func(0x710c1b15a0ULL, [](Arm64Context *c) {
        uint64_t out_count_ptr = c->x[0];
        uint64_t out_uids_ptr = c->x[1];
        int max_uids = static_cast<int>(c->x[2]);
        if (out_count_ptr && out_count_ptr >= 0x7000000000ULL && out_count_ptr < 0x7300000000ULL) {
            *reinterpret_cast<int32_t *>(out_count_ptr) = 1;
        }
        if (out_uids_ptr && max_uids > 0 && out_uids_ptr >= 0x7000000000ULL && out_uids_ptr < 0x7300000000ULL) {
            std::memcpy(reinterpret_cast<void *>(out_uids_ptr), s_default_uid, 16);
        }
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook nn::account::ListAllUsers (0x710c1b0270)
    totk::recompiler::register_recompiled_func(0x710c1b0270ULL, [](Arm64Context *c) {
        uint64_t out_count_ptr = c->x[0];
        uint64_t out_uids_ptr = c->x[1];
        int max_uids = static_cast<int>(c->x[2]);
        if (out_count_ptr && out_count_ptr >= 0x7000000000ULL && out_count_ptr < 0x7300000000ULL) {
            *reinterpret_cast<int32_t *>(out_count_ptr) = 1;
        }
        if (out_uids_ptr && max_uids > 0 && out_uids_ptr >= 0x7000000000ULL && out_uids_ptr < 0x7300000000ULL) {
            std::memcpy(reinterpret_cast<void *>(out_uids_ptr), s_default_uid, 16);
        }
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook nn::account::PopOpenUsers (0x710c1b12c0)
    totk::recompiler::register_recompiled_func(0x710c1b12c0ULL, [](Arm64Context *c) {
        uint64_t out_count_ptr = c->x[0];
        uint64_t out_handles_ptr = c->x[1];
        int max_handles = static_cast<int>(c->x[2]);
        if (out_count_ptr && out_count_ptr >= 0x7000000000ULL && out_count_ptr < 0x7300000000ULL) {
            *reinterpret_cast<int32_t *>(out_count_ptr) = 1;
        }
        if (out_handles_ptr && max_handles > 0 && out_handles_ptr >= 0x7000000000ULL && out_handles_ptr < 0x7300000000ULL) {
            *reinterpret_cast<uint64_t *>(out_handles_ptr) = 0x100ULL;
        }
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook nn::account::GetLastOpenedUser (0x710c1b11b0)
    totk::recompiler::register_recompiled_func(0x710c1b11b0ULL, [](Arm64Context *c) {
        uint64_t out_uid_ptr = c->x[0];
        if (out_uid_ptr && out_uid_ptr >= 0x7000000000ULL && out_uid_ptr < 0x7300000000ULL) {
            std::memcpy(reinterpret_cast<void *>(out_uid_ptr), s_default_uid, 16);
        }
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook nn::account::OpenUser (0x710c1b1200)
    totk::recompiler::register_recompiled_func(0x710c1b1200ULL, [](Arm64Context *c) {
        uint64_t out_handle_ptr = c->x[0];
        if (out_handle_ptr && out_handle_ptr >= 0x7000000000ULL && out_handle_ptr < 0x7300000000ULL) {
            *reinterpret_cast<uint64_t *>(out_handle_ptr) = 0x100ULL;
        }
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook ZsDic loader (0x7104cb6528 and 0x7104cb62ec)
    totk::recompiler::register_recompiled_func(0x7104cb6528ULL, [](Arm64Context *c) {
        std::printf("  [HLR Hook] f_7104cb6528 (ZsDic loader) -> returning NULL (not found)\n");
        std::fflush(stdout);
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    totk::recompiler::register_recompiled_func(0x7104cb62ecULL, [](Arm64Context *c) {
        std::printf("  [HLR Hook] f_7104cb62ec (ZsDic system load) -> returning 0 (bypassed)\n");
        std::fflush(stdout);
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook f_7104c9acf4 (safe null return)
    totk::recompiler::register_recompiled_func(0x7104c9acf4ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
        return;
    });

    // Hook f_7104c904d0 to avoid dereferencing an invalid object or vtable.
    totk::recompiler::register_recompiled_func(0x7104c904d0ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
        return;
    });

    // Hook sead::ResourceMgr::load (0x7104214ea8)
    totk::recompiler::register_recompiled_func(0x7104214ea8ULL, [](Arm64Context *c) {
        std::printf("  [HLR Hook] sead::ResourceMgr::load (0x7104214ea8) from LR=0x%llx -> returning dummy resource\n",
                    static_cast<unsigned long long>(c->x[30]));
        std::fflush(stdout);
        c->x[0] = get_dummy_sead_resource();
        c->pc = c->x[30];
    });

    // Hook sead::ResourceMgr::tryLoad (0x7104215360)
    totk::recompiler::register_recompiled_func(0x7104215360ULL, [](Arm64Context *c) {
        std::printf("  [HLR Hook] sead::ResourceMgr::tryLoad (0x7104215360) from LR=0x%llx -> returning dummy resource\n",
                    static_cast<unsigned long long>(c->x[30]));
        std::fflush(stdout);
        c->x[0] = get_dummy_sead_resource();
        c->pc = c->x[30];
    });

    // Hook f_7104cb4a40 (system archive/resource package initializer)
    totk::recompiler::register_recompiled_func(0x7104cb4a40ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook BootupBackgroundWorkThread (0x7104b574ac)
    totk::recompiler::register_recompiled_func(0x7104b574acULL, [](Arm64Context *c) {
        std::printf("  [HLR Hook] f_7104b574ac (BootupBackgroundWorkThread) -> bypassed\n");
        std::fflush(stdout);
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook f_7104cf8f9c (Module Registrar - guard against null module)
    totk::recompiler::register_recompiled_func(0x7104cf8f9cULL, [](Arm64Context *c) {
        uint64_t ptr_to_res = c->x[1];
        uint64_t res = (ptr_to_res >= 0x7000000000ULL && ptr_to_res < 0x7300000000ULL)
                       ? *reinterpret_cast<uint64_t *>(ptr_to_res) : 0;
        if (!res) {
            std::printf("  [HLR Hook] f_7104cf8f9c: null resource passed from LR=0x%llx -> skipping\n",
                        static_cast<unsigned long long>(c->x[30]));
            std::fflush(stdout);
            c->x[0] = 0;
            c->pc = c->x[30];
            return;
        }
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook f_7104cc449c (Subsystem Module Initializer)
    totk::recompiler::register_recompiled_func(0x7104cc449cULL, [](Arm64Context *c) {
        std::printf("  [HLR Hook] f_7104cc449c (Subsystem Module Initializer) -> clean return 0\n");
        std::fflush(stdout);
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook f_7104884e30 (Archive unpacker)
    totk::recompiler::register_recompiled_func(0x7104884e30ULL, [](Arm64Context *c) {
        std::printf("  [HLR Hook] f_7104884e30 (Archive unpacker) -> clean return 0\n");
        std::fflush(stdout);
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook f_7104c59790 (Display / Viewport Config Initializer)
    totk::recompiler::register_recompiled_func(0x7104c59790ULL, [](Arm64Context *c) {
        uint64_t out_struct = c->x[0];
        if (out_struct >= 0x7000000000ULL && out_struct < 0x7300000000ULL) {
            std::memset(reinterpret_cast<void *>(out_struct), 0, 0x88);
            *reinterpret_cast<uint32_t *>(out_struct + 0x80) = 1;
        }
        std::printf("  [HLR Hook] f_7104c59790 (Config Init) struct=0x%llx -> initialized\n",
                    static_cast<unsigned long long>(out_struct));
        std::fflush(stdout);
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Ensure display/screen singleton at 0x7107accde8 is non-null
    uint64_t singleton_ptr_addr = 0x7107accde8ULL;
    if (singleton_ptr_addr >= 0x7000000000ULL && singleton_ptr_addr < 0x7300000000ULL) {
        uint64_t *p_instance = reinterpret_cast<uint64_t *>(singleton_ptr_addr);
        if (*p_instance == 0) {
            uint64_t dummy_obj = hlr_bump_alloc(0x1000, 0x40);
            uint64_t dummy_vtbl = hlr_bump_alloc(0x400, 0x20);
            uint64_t dummy_arena = hlr_bump_alloc(0x200000, 0x1000); // 2 MiB committed arena
            uint64_t dummy_dim = hlr_bump_alloc(0x40, 0x10);
            *reinterpret_cast<float *>(dummy_dim + 0x8) = 1280.0f;
            *reinterpret_cast<float *>(dummy_dim + 0xc) = 720.0f;
            *reinterpret_cast<uint64_t *>(dummy_obj) = dummy_vtbl;
            *reinterpret_cast<uint64_t *>(dummy_obj + 0x40) = dummy_arena;
            *reinterpret_cast<uint64_t *>(dummy_obj + 0x150) = dummy_dim;
            *reinterpret_cast<uint64_t *>(dummy_obj + 0x168) = dummy_arena;
            *reinterpret_cast<uint64_t *>(dummy_obj + 0x170) = dummy_arena;
            *p_instance = dummy_obj;
            std::printf("  [HLR] Initialized Display singleton at 0x%llx -> dummy obj 0x%llx (arena=0x%llx)\n",
                        static_cast<unsigned long long>(singleton_ptr_addr),
                        static_cast<unsigned long long>(dummy_obj),
                        static_cast<unsigned long long>(dummy_arena));
        }
    }

    // Hook nn::os::LockMutex (0x710c13f5a0)
    totk::recompiler::register_recompiled_func(0x710c13f5a0ULL, [](Arm64Context *c) {
        uint64_t mtx = c->x[0];
        if (mtx < 0x7000000000ULL || mtx >= 0x7300000000ULL) {
            static std::unordered_set<uint64_t> s_warned;
            if (s_warned.insert(c->x[30]).second) {
                std::printf("  [LockMutex Guard] Ignored invalid mutex 0x%llx from LR=0x%llx\n",
                            static_cast<unsigned long long>(mtx),
                            static_cast<unsigned long long>(c->x[30]));
                std::fflush(stdout);
            }
            c->x[0] = 0;
            c->pc = c->x[30];
            return;
        }
        uint64_t cur_thr = (c->tpidrro >= 0x7000000000ULL && c->tpidrro < 0x7300000000ULL)
                           ? *reinterpret_cast<uint64_t *>(c->tpidrro + 0x1f8) : 1;
        uint64_t owner = *reinterpret_cast<uint64_t *>(mtx + 0x10);
        uint32_t *p_rec = reinterpret_cast<uint32_t *>(mtx + 0x8);
        if (owner == cur_thr) {
            (*p_rec)++;
        } else {
            *reinterpret_cast<uint64_t *>(mtx + 0x10) = cur_thr;
            *p_rec = 1;
        }
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook nn::os::TryLockMutex (0x710c13f5f0)
    totk::recompiler::register_recompiled_func(0x710c13f5f0ULL, [](Arm64Context *c) {
        uint64_t mtx = c->x[0];
        if (mtx < 0x7000000000ULL || mtx >= 0x7300000000ULL) {
            c->x[0] = 0;
            c->pc = c->x[30];
            return;
        }
        uint64_t cur_thr = (c->tpidrro >= 0x7000000000ULL && c->tpidrro < 0x7300000000ULL)
                           ? *reinterpret_cast<uint64_t *>(c->tpidrro + 0x1f8) : 1;
        uint64_t owner = *reinterpret_cast<uint64_t *>(mtx + 0x10);
        uint32_t *p_rec = reinterpret_cast<uint32_t *>(mtx + 0x8);
        if (owner == 0 || owner == cur_thr) {
            *reinterpret_cast<uint64_t *>(mtx + 0x10) = cur_thr;
            (*p_rec)++;
            c->x[0] = 1;
        } else {
            c->x[0] = 0;
        }
        c->pc = c->x[30];
    });

    // Hook nn::os::UnlockMutex (0x710c13f660)
    totk::recompiler::register_recompiled_func(0x710c13f660ULL, [](Arm64Context *c) {
        uint64_t mtx = c->x[0];
        if (mtx >= 0x7000000000ULL && mtx < 0x7300000000ULL) {
            uint32_t *p_rec = reinterpret_cast<uint32_t *>(mtx + 0x8);
            if (*p_rec > 0) {
                (*p_rec)--;
                if (*p_rec == 0) {
                    *reinterpret_cast<uint64_t *>(mtx + 0x10) = 0;
                }
            }
        }
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook f_71048cb3dc (RenderBuffer setup)
    totk::recompiler::register_recompiled_func(0x71048cb3dcULL, [](Arm64Context *c) {
        std::printf("  [HLR Hook] f_71048cb3dc (RenderBuffer init) -> clean return\n");
        std::fflush(stdout);
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook f_7104957410 (agl texture descriptor setup)
    totk::recompiler::register_recompiled_func(0x7104957410ULL, [](Arm64Context *c) {
        std::printf("  [HLR Hook] f_7104957410 (Texture Descriptor Setup) -> clean return\n");
        std::fflush(stdout);
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook f_71048ca028 (agl::lyr pipeline / shader descriptors batch init)
    totk::recompiler::register_recompiled_func(0x71048ca028ULL, [](Arm64Context *c) {
        std::printf("  [HLR Hook] f_71048ca028 (agl::lyr pipeline batch init) -> clean return\n");
        std::fflush(stdout);
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook f_71048d2330 (agl::lyr sub-pipeline layout setup)
    totk::recompiler::register_recompiled_func(0x71048d2330ULL, [](Arm64Context *c) {
        std::printf("  [HLR Hook] f_71048d2330 (agl::lyr pipeline layout setup) -> clean return\n");
        std::fflush(stdout);
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook f_71048cad2c (agl::lyr companion pipeline layout setup)
    totk::recompiler::register_recompiled_func(0x71048cad2cULL, [](Arm64Context *c) {
        std::printf("  [HLR Hook] f_71048cad2c (agl::lyr companion layout setup) -> clean return\n");
        std::fflush(stdout);
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Hook f_71048c3e3c (agl::lyr screen dimension / viewport getter)
    totk::recompiler::register_recompiled_func(0x71048c3e3cULL, [](Arm64Context *c) {
        uint64_t obj = c->x[0];
        if (obj >= 0x7000000000ULL && obj < 0x7300000000ULL) {
            uint64_t *p_dim = reinterpret_cast<uint64_t *>(obj + 0x150);
            if (*p_dim == 0) {
                uint64_t dim = hlr_bump_alloc(0x40, 0x10);
                *reinterpret_cast<float *>(dim + 0x8) = 1280.0f;
                *reinterpret_cast<float *>(dim + 0xc) = 720.0f;
                *p_dim = dim;
            }
        }
        std::printf("  [HLR Hook] f_71048c3e3c (Screen Dimension Getter) -> populated 1280x720\n");
        std::fflush(stdout);
        c->pc = c->x[30];
    });

    // Hook f_71048c548c (agl::lyr table slot clear)
    totk::recompiler::register_recompiled_func(0x71048c548cULL, [](Arm64Context *c) {
        c->pc = c->x[30];
    });

    // Hook f_71046effac (agl::lyr render target setup)
    totk::recompiler::register_recompiled_func(0x71046effacULL, [](Arm64Context *c) {
        std::printf("  [HLR Hook] f_71046effac (agl::lyr render target setup) -> clean return\n");
        std::fflush(stdout);
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Safe HLR implementations for Horizon OS TLS (GetTlsValue / SetTlsValue)
    static thread_local uint64_t t_host_tls_slots[64] = {0};

    auto hlr_get_tls = [](Arm64Context *c) {
        uint32_t slot = static_cast<uint32_t>(c->x[0]);
        uint64_t val = (slot < 64) ? t_host_tls_slots[slot] : 0;
        if (val == 0 && c->tpidrro >= 0x7000000000ULL && c->tpidrro < 0x7300000000ULL) {
            uint64_t thr = *reinterpret_cast<uint64_t *>(c->tpidrro + 0x1f8);
            if (thr >= 0x7000000000ULL && thr < 0x7300000000ULL) {
                val = *reinterpret_cast<uint64_t *>(thr + 0x80 + (static_cast<uint64_t>(slot) << 3));
            }
        }
        c->x[0] = val;
        c->pc = c->x[30];
    };

    auto hlr_set_tls = [](Arm64Context *c) {
        uint32_t slot = static_cast<uint32_t>(c->x[0]);
        uint64_t val = c->x[1];
        if (slot < 64) {
            t_host_tls_slots[slot] = val;
        }
        if (c->tpidrro >= 0x7000000000ULL && c->tpidrro < 0x7300000000ULL) {
            uint64_t thr = *reinterpret_cast<uint64_t *>(c->tpidrro + 0x1f8);
            if (thr >= 0x7000000000ULL && thr < 0x7300000000ULL) {
                *reinterpret_cast<uint64_t *>(thr + 0x80 + (static_cast<uint64_t>(slot) << 3)) = val;
            }
        }
        c->pc = c->x[30];
    };

    totk::recompiler::register_recompiled_func(0x710c143c60ULL, hlr_get_tls);
    totk::recompiler::register_recompiled_func(0x710c144940ULL, hlr_get_tls);
    totk::recompiler::register_recompiled_func(0x710c143c80ULL, hlr_set_tls);
    totk::recompiler::register_recompiled_func(0x710c144950ULL, hlr_set_tls);

    // Guard f_71046fe6ac default heap parameter
    static totk::recompiler::RecompiledFunc s_orig_71046fe6ac =
        totk::recompiler::lookup_recompiled_func(0x71046fe6acULL);
    totk::recompiler::register_recompiled_func(0x71046fe6acULL, [](Arm64Context *c) {
        if (c->x[2] == 0) {
            c->x[2] = get_default_heap();
        }

        static uint64_t s_dummy_lyr = 0;
        if (s_dummy_lyr == 0) {
            s_dummy_lyr = hlr_bump_alloc(0x200, 0x20);
            uint64_t vtbl = hlr_bump_alloc(0x200, 0x20);
            for (int i = 0; i < 0x200 / 8; ++i) {
                *reinterpret_cast<uint64_t *>(vtbl + i * 8) = 0x7104059df0ULL;
            }
            *reinterpret_cast<uint64_t *>(s_dummy_lyr) = vtbl;
        }
        if (c->x[1] == 0) {
            c->x[1] = s_dummy_lyr;
        } else if (c->x[1] >= 0x7000000000ULL && c->x[1] < 0x7300000000ULL) {
            uint64_t *p_v = reinterpret_cast<uint64_t *>(c->x[1]);
            if (*p_v == 0) {
                *p_v = *reinterpret_cast<uint64_t *>(s_dummy_lyr);
            }
        }
        if (s_orig_71046fe6ac) {
            s_orig_71046fe6ac(c);
        } else {
            c->pc = c->x[30];
        }
    });

    // Hook sead::HeapMgr::getRootHeap / getDefaultHeap (0x71048ac230)
    totk::recompiler::register_recompiled_func(0x71048ac230ULL, [](Arm64Context *c) {
        c->x[0] = get_default_heap();
        c->pc = c->x[30];
    });

    // Hook f_7104169824 (sead::Heap create child heap)
    static totk::recompiler::RecompiledFunc s_orig_7104169824 =
        totk::recompiler::lookup_recompiled_func(0x7104169824ULL);
    totk::recompiler::register_recompiled_func(0x7104169824ULL, [](Arm64Context *c) {
        if (c->x[2] == 0) {
            c->x[2] = get_default_heap();
        }
        if (s_orig_7104169824) {
            s_orig_7104169824(c);
        }
        if (c->x[0] == 0) {
            c->x[0] = get_default_heap();
        }
        c->pc = c->x[30];
    });

    // Hook f_71041d1ed0 (sead::Heap find or create child heap)
    totk::recompiler::register_recompiled_func(0x71041d1ed0ULL, [](Arm64Context *c) {
        if (c->x[2] == 0) {
            c->x[2] = get_default_heap();
        }
        c->x[0] = get_default_heap();
        c->pc = c->x[30];
    });

    // Hook 0x71049b7b88 (resource loader wrapper -> dummy resource)
    totk::recompiler::register_recompiled_func(0x71049b7b88ULL, [](Arm64Context *c) {
        static uint64_t s_dummy_res = 0;
        if (s_dummy_res == 0) {
            s_dummy_res = hlr_bump_alloc(0x400, 0x40);
            uint64_t vtbl = hlr_bump_alloc(0x200, 0x20);
            for (int i = 0; i < 0x200 / 8; ++i) {
                *reinterpret_cast<uint64_t *>(vtbl + i * 8) = 0x7180000010ULL;
            }
            *reinterpret_cast<uint64_t *>(s_dummy_res + 0x0) = vtbl;
            *reinterpret_cast<uint64_t *>(s_dummy_res + 0x8) = s_dummy_res + 0x40; // data buffer
            *reinterpret_cast<uint64_t *>(s_dummy_res + 0x10) = 0x1000;            // size
            *reinterpret_cast<uint64_t *>(s_dummy_res + 0x28) = 1;                 // refcount
            *reinterpret_cast<uint64_t *>(s_dummy_res + 0x30) = get_default_heap();
        }
        c->x[0] = s_dummy_res;
        c->pc = c->x[30];
    });

    static totk::recompiler::RecompiledFunc s_orig_71049b9afc =
        totk::recompiler::lookup_recompiled_func(0x71049b9afcULL);
    totk::recompiler::register_recompiled_func(0x71049b9afcULL, [](Arm64Context *c) {
        constexpr uint64_t guest_base = 0x7000000000ULL;
        constexpr uint64_t guest_limit = 0x7300000000ULL;
        if (c->x[0] < guest_base || c->x[0] >= guest_limit) {
            c->pc = c->x[30];
            return;
        }
        if (c->x[0] > guest_limit - 0x320) {
            c->pc = c->x[30];
            return;
        }

        uint64_t *node_slot = reinterpret_cast<uint64_t *>(c->x[0] + 0x318);
        const uint64_t node = *node_slot;
        if (node != 0 && (node < guest_base || node >= guest_limit - 0x20)) {
            *node_slot = 0;
        }
        if (c->x[1] < guest_base || c->x[1] >= guest_limit) {
            c->x[1] = hlr_bump_alloc(0x100, 0x10);
        }

        if (s_orig_71049b9afc) s_orig_71049b9afc(c);
        else c->pc = c->x[30];
    });

    static totk::recompiler::RecompiledFunc s_orig_71049b7c88 =
        totk::recompiler::lookup_recompiled_func(0x71049b7c88ULL);
    totk::recompiler::register_recompiled_func(0x71049b7c88ULL, [](Arm64Context *c) {
        if (c->x[0] < 0x7000000000ULL || c->x[0] >= 0x7300000000ULL) {
            c->x[0] = get_safe_container_node();
        }
        if (s_orig_71049b7c88) {
            s_orig_71049b7c88(c);
        } else {
            c->pc = c->x[30];
        }
        if (c->x[0] < 0x7000000000ULL || c->x[0] >= 0x7300000000ULL) {
            c->x[0] = get_safe_container_node();
        }
    });

    static totk::recompiler::RecompiledFunc s_orig_71049b7fb8 =
        totk::recompiler::lookup_recompiled_func(0x71049b7fb8ULL);
    totk::recompiler::register_recompiled_func(0x71049b7fb8ULL, [](Arm64Context *c) {
        if (c->x[0] < 0x7000000000ULL || c->x[0] >= 0x7300000000ULL) {
            c->x[0] = get_safe_container_node();
        }
        if (s_orig_71049b7fb8) {
            s_orig_71049b7fb8(c);
        } else {
            c->pc = c->x[30];
        }
        if (c->x[0] < 0x7000000000ULL || c->x[0] >= 0x7300000000ULL) {
            c->x[0] = get_safe_container_node();
        }
    });

    static totk::recompiler::RecompiledFunc s_orig_71049b8030 =
        totk::recompiler::lookup_recompiled_func(0x71049b8030ULL);
    totk::recompiler::register_recompiled_func(0x71049b8030ULL, [](Arm64Context *c) {
        if (c->x[0] < 0x7000000000ULL || c->x[0] >= 0x7300000000ULL) {
            c->x[0] = get_safe_container_node();
        }
        if (s_orig_71049b8030) {
            s_orig_71049b8030(c);
        } else {
            c->pc = c->x[30];
        }
        if (c->x[0] < 0x7000000000ULL || c->x[0] >= 0x7300000000ULL) {
            c->x[0] = get_safe_container_node();
        }
    });

    static totk::recompiler::RecompiledFunc s_orig_71049b80a8 =
        totk::recompiler::lookup_recompiled_func(0x71049b80a8ULL);
    totk::recompiler::register_recompiled_func(0x71049b80a8ULL, [](Arm64Context *c) {
        if (c->x[0] < 0x7000000000ULL || c->x[0] >= 0x7300000000ULL) {
            c->x[0] = get_safe_container_node();
        }
        if (s_orig_71049b80a8) {
            s_orig_71049b80a8(c);
        } else {
            c->pc = c->x[30];
        }
        if (c->x[0] < 0x7000000000ULL || c->x[0] >= 0x7300000000ULL) {
            c->x[0] = get_safe_container_node();
        }
    });

    static totk::recompiler::RecompiledFunc s_orig_71049b8120 =
        totk::recompiler::lookup_recompiled_func(0x71049b8120ULL);
    totk::recompiler::register_recompiled_func(0x71049b8120ULL, [](Arm64Context *c) {
        if (c->x[0] < 0x7000000000ULL || c->x[0] >= 0x7300000000ULL) {
            c->x[0] = get_safe_container_node();
        }
        if (s_orig_71049b8120) {
            s_orig_71049b8120(c);
        } else {
            c->pc = c->x[30];
        }
        if (c->x[0] < 0x7000000000ULL || c->x[0] >= 0x7300000000ULL) {
            c->x[0] = get_safe_container_node();
        }
    });

    // Guard f_7104658020 (null target setter guard)
    static totk::recompiler::RecompiledFunc s_orig_7104658020 =
        totk::recompiler::lookup_recompiled_func(0x7104658020ULL);
    totk::recompiler::register_recompiled_func(0x7104658020ULL, [](Arm64Context *c) {
        if (c->x[0] == 0) {
            c->pc = c->x[30];
            return;
        }
        if (s_orig_7104658020) {
            s_orig_7104658020(c);
        } else {
            c->pc = c->x[30];
        }
    });

    // Hook f_710492ddec (sead::Thread::wait / join -> non-blocking return)
    totk::recompiler::register_recompiled_func(0x710492ddecULL, [](Arm64Context *c) {
        std::printf("  [HLR Hook] sead::Thread::wait(0x%llx) -> joined\n",
                    static_cast<unsigned long long>(c->x[0]));
        std::fflush(stdout);
        c->pc = c->x[30];
    });

    // Guard f_710465794c (agl / sead heap-backed resource init)
    static totk::recompiler::RecompiledFunc s_orig_710465794c =
        totk::recompiler::lookup_recompiled_func(0x710465794cULL);
    totk::recompiler::register_recompiled_func(0x710465794cULL, [](Arm64Context *c) {
        if (c->x[1] >= 0x7000000000ULL && c->x[1] < 0x7300000000ULL) {
            uint64_t *p_heap = reinterpret_cast<uint64_t *>(c->x[1]);
            if (*p_heap == 0) {
                *p_heap = get_default_heap();
            }
        }
        if (s_orig_710465794c) {
            s_orig_710465794c(c);
        } else {
            c->pc = c->x[30];
        }
    });

    // Guard f_71048f0d70 (agl layer table builder)
    totk::recompiler::register_recompiled_func(0x71048f0d70ULL, [](Arm64Context *c) {
        std::printf("  [HLR Hook] f_71048f0d70 (agl layer table builder) -> clean return\n");
        std::fflush(stdout);
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Guard f_7104d24dec (sead child heap create: fallback parent heap to default root heap)
    static totk::recompiler::RecompiledFunc s_orig_7104d24dec =
        totk::recompiler::lookup_recompiled_func(0x7104d24decULL);
    totk::recompiler::register_recompiled_func(0x7104d24decULL, [](Arm64Context *c) {
        if (c->x[2] == 0) {
            c->x[2] = get_default_heap();
        }
        if (s_orig_7104d24dec) {
            s_orig_7104d24dec(c);
        } else {
            c->x[0] = get_default_heap();
            c->pc = c->x[30];
        }
    });

    // Guard ReaderWriterLock functions against NULL pointers
    static totk::recompiler::RecompiledFunc s_orig_rw_rel =
        totk::recompiler::lookup_recompiled_func(0x710c14a5c0ULL);
    totk::recompiler::register_recompiled_func(0x710c14a5c0ULL, [](Arm64Context *c) {
        if (c->x[0] == 0) {
            c->x[0] = 0;
            c->pc = c->x[30];
            return;
        }
        if (s_orig_rw_rel) {
            s_orig_rw_rel(c);
        } else {
            c->x[0] = 0;
            c->pc = c->x[30];
        }
    });

    static totk::recompiler::RecompiledFunc s_orig_rw_acq =
        totk::recompiler::lookup_recompiled_func(0x710c14a340ULL);
    totk::recompiler::register_recompiled_func(0x710c14a340ULL, [](Arm64Context *c) {
        if (c->x[0] == 0) {
            c->x[0] = 0;
            c->pc = c->x[30];
            return;
        }
        if (s_orig_rw_acq) {
            s_orig_rw_acq(c);
        } else {
            c->x[0] = 0;
            c->pc = c->x[30];
        }
    });

    totk::recompiler::register_recompiled_func(0x7106bb7510ULL, [](Arm64Context *c) {
        if (c->x[0] == 0) {
            c->x[0] = 0;
            c->pc = c->x[30];
            return;
        }
        if (s_orig_rw_rel) {
            s_orig_rw_rel(c);
        } else {
            c->x[0] = 0;
            c->pc = c->x[30];
        }
    });

    totk::recompiler::register_recompiled_func(0x7106bb7500ULL, [](Arm64Context *c) {
        if (c->x[0] == 0) {
            c->x[0] = 0;
            c->pc = c->x[30];
            return;
        }
        if (s_orig_rw_acq) {
            s_orig_rw_acq(c);
        } else {
            c->x[0] = 0;
            c->pc = c->x[30];
        }
    });

    // Hook sead::ThreadMgr::getThread (0x7106bb74c0)
    auto s_thread_dummy_method = [](Arm64Context *c) {
        c->x[0] = 1; // Thread ID = 1
        c->pc = c->x[30];
    };
    totk::recompiler::register_recompiled_func(0x7180000010ULL, s_thread_dummy_method);

    totk::recompiler::register_recompiled_func(0x7106bb74c0ULL, [](Arm64Context *c) {
        static uint64_t s_thread_obj = 0;
        static uint64_t s_thread_vtbl = 0;
        if (s_thread_obj == 0) {
            s_thread_obj = hlr_bump_alloc(0x400, 0x40);
            s_thread_vtbl = hlr_bump_alloc(0x400, 0x20);
            for (int i = 0; i < 0x400 / 8; ++i) {
                *reinterpret_cast<uint64_t *>(s_thread_vtbl + i * 8) = 0x7180000010ULL;
            }
            *reinterpret_cast<uint32_t *>(s_thread_obj + 0xb0) = 1; // thread id / attr
        }
        // Always refresh vtable and thread heap pointers
        *reinterpret_cast<uint64_t *>(s_thread_obj) = s_thread_vtbl;
        *reinterpret_cast<uint64_t *>(s_thread_obj + 0x88) = get_default_heap();
        c->x[0] = s_thread_obj;
        c->pc = c->x[30];
    });

    // Guard f_7104b10538 (palette / world manager init)
    static totk::recompiler::RecompiledFunc s_orig_7104b10538 =
        totk::recompiler::lookup_recompiled_func(0x7104b10538ULL);
    totk::recompiler::register_recompiled_func(0x7104b10538ULL, [](Arm64Context *c) {
        uint64_t def_heap = get_default_heap();
        uint64_t *p_ptr = reinterpret_cast<uint64_t *>(0x710795da80ULL);
        if (*p_ptr == 0 || *reinterpret_cast<uint64_t *>(*p_ptr) == 0) {
            static uint64_t s_box = 0;
            static uint64_t s_mgr = 0;
            if (s_box == 0) {
                s_box = hlr_bump_alloc(0x100, 0x20);
                s_mgr = hlr_bump_alloc(0x200, 0x40);
                *reinterpret_cast<uint64_t *>(s_box) = s_mgr;
            }
            *reinterpret_cast<uint64_t *>(s_mgr + 0x8) = def_heap;
            *p_ptr = s_box;
        } else {
            uint64_t existing_inst = *reinterpret_cast<uint64_t *>(*p_ptr);
            *reinterpret_cast<uint64_t *>(existing_inst + 0x8) = def_heap;
        }

        uint64_t inst1 = p_ptr ? *p_ptr : 0;
        uint64_t inst2 = inst1 ? *reinterpret_cast<uint64_t *>(inst1) : 0;
        std::printf("  [HLR Hook f_7104b10538] p_ptr=%p *p_ptr=0x%llx **p_ptr=0x%llx def_heap=0x%llx\n",
                    p_ptr, static_cast<unsigned long long>(inst1),
                    static_cast<unsigned long long>(inst2),
                    static_cast<unsigned long long>(def_heap));
        std::fflush(stdout);

        if (s_orig_7104b10538) {
            s_orig_7104b10538(c);
        } else {
            c->pc = c->x[30];
        }
    });

    // Guard f_7104154bd0 queue buffer
    static totk::recompiler::RecompiledFunc s_orig_7104154bd0 =
        totk::recompiler::lookup_recompiled_func(0x7104154bd0ULL);
    totk::recompiler::register_recompiled_func(0x7104154bd0ULL, [](Arm64Context *c) {
        uint64_t obj = c->x[0];
        // If called by a worker thread, yield and return cleanly (no work pending yet)
        if (c->sp >= 0x7200000000ULL && c->sp < 0x7300000000ULL) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            c->x[0] = 0;
            c->pc = c->x[30];
            return;
        }

        if (obj >= 0x7000000000ULL && obj < 0x7300000000ULL) {
            uint64_t q = *reinterpret_cast<uint64_t *>(obj + 0x148);
            if (q >= 0x7000000000ULL && q < 0x7300000000ULL) {
                uint64_t *p_buf = reinterpret_cast<uint64_t *>(q + 0x30);
                if (*p_buf == 0) {
                    *p_buf = hlr_bump_alloc(0x20000, 0x40);
                }
            }
        }
        if (s_orig_7104154bd0) {
            s_orig_7104154bd0(c);
        } else {
            c->pc = c->x[30];
        }
    });

    // Clamp the SEAD/gsys lock table count before allocating mutex entries.
    static totk::recompiler::RecompiledFunc s_orig_7104cccf80 =
        totk::recompiler::lookup_recompiled_func(0x7104cccf80ULL);
    totk::recompiler::register_recompiled_func(0x7104cccf80ULL, [](Arm64Context *c) {
        if (c->x[2] > 64ULL) {
            c->x[2] = std::min<uint64_t>(c->x[2], 64ULL);
        }
        if (s_orig_7104cccf80) {
            s_orig_7104cccf80(c);
        } else {
            c->pc = c->x[30];
        }
    });

    // Guard f_7104cb6dac (ares::System array allocator)
    static totk::recompiler::RecompiledFunc s_orig_7104cb6dac =
        totk::recompiler::lookup_recompiled_func(0x7104cb6dacULL);
    totk::recompiler::register_recompiled_func(0x7104cb6dacULL, [](Arm64Context *c) {
        uint64_t dst = c->x[0];
        uint32_t count = static_cast<uint32_t>(c->x[1]);
        uint64_t heap = c->x[2];
        std::printf("  [HLR Hook] f_7104cb6dac ENTER: dst=0x%llx count=%u heap=0x%llx\n",
                    static_cast<unsigned long long>(dst), count,
                    static_cast<unsigned long long>(heap));
        if (count == 0) {
            count = 32;
            c->x[1] = 32;
        }
        if (dst >= 0x7000000000ULL && dst < 0x7300000000ULL) {
            uint64_t *p_buf = reinterpret_cast<uint64_t *>(dst + 0x8);
            if (*p_buf == 0) {
                *p_buf = hlr_bump_alloc(count * 64, 0x40);
                *reinterpret_cast<uint32_t *>(dst + 0x0) = count;
                std::printf("  [HLR Hook] f_7104cb6dac pre-allocated buffer 0x%llx (count=%u)\n",
                            static_cast<unsigned long long>(*p_buf), count);
            }
        }
        std::fflush(stdout);
        if (s_orig_7104cb6dac) {
            s_orig_7104cb6dac(c);
        } else {
            c->pc = c->x[30];
        }
    });

    // Guard f_7104cb69fc (ares::System subsystem manager)
    static totk::recompiler::RecompiledFunc s_orig_7104cb69fc =
        totk::recompiler::lookup_recompiled_func(0x7104cb69fcULL);
    totk::recompiler::register_recompiled_func(0x7104cb69fcULL, [](Arm64Context *c) {
        uint64_t obj = c->x[0];
        uint64_t cfg = c->x[1];
        std::printf("  [HLR Hook] f_7104cb69fc ENTER: obj=0x%llx cfg=0x%llx\n",
                    static_cast<unsigned long long>(obj),
                    static_cast<unsigned long long>(cfg));
        if (obj >= 0x7000000000ULL && obj < 0x7300000000ULL) {
            uint64_t *p_arr = reinterpret_cast<uint64_t *>(obj + 0x18);
            if (*p_arr == 0) {
                *p_arr = hlr_bump_alloc(0x4000, 0x40);
                *reinterpret_cast<uint32_t *>(obj + 0x10) = 64;
                std::printf("  [HLR Hook] f_7104cb69fc pre-allocated array at obj+0x18 = 0x%llx\n",
                            static_cast<unsigned long long>(*p_arr));
            }
        }
        std::fflush(stdout);
        if (s_orig_7104cb69fc) {
            s_orig_7104cb69fc(c);
        } else {
            c->pc = c->x[30];
        }
    });

    // Guard f_7104709240 (display / layer buffer descriptor init)
    totk::recompiler::register_recompiled_func(0x7104709240ULL, [](Arm64Context *c) {
        std::printf("  [HLR Hook] f_7104709240 (layer buffer descriptor init) -> clean return\n");
        c->pc = c->x[30];
    });

    static totk::recompiler::RecompiledFunc s_orig_7104059b90 =
        totk::recompiler::lookup_recompiled_func(0x7104059b90ULL);
    totk::recompiler::register_recompiled_func(0x7104059b90ULL, [](Arm64Context *c) {
        auto &memory = totk::runtime::MemoryManager::instance();
        if (c->x[0] == 0 || !is_mapped_guest_range(memory, c->x[0], 0x48)) {
            c->x[0] = 0;
            c->pc = c->x[30];
            return;
        }
        if (s_orig_7104059b90) {
            s_orig_7104059b90(c);
        } else {
            c->pc = c->x[30];
        }
    });

    // Guard SEAD container setup functions against invalid container and heap pointers.
    static totk::recompiler::RecompiledFunc s_orig_7104708408 =
        totk::recompiler::lookup_recompiled_func(0x7104708408ULL);
    totk::recompiler::register_recompiled_func(0x7104708408ULL, [](Arm64Context *c) {
        auto &memory = totk::runtime::MemoryManager::instance();
        if (c->x[0] == 0 || !is_mapped_guest_range(memory, c->x[0], sizeof(uint64_t)) ||
            c->x[2] == 0 || !is_mapped_guest_range(memory, c->x[2], sizeof(uint64_t))) {
            c->pc = c->x[30];
            return;
        }
        if (s_orig_7104708408) {
            s_orig_7104708408(c);
        } else {
            c->pc = c->x[30];
        }
    });

    static totk::recompiler::RecompiledFunc s_orig_710470851c =
        totk::recompiler::lookup_recompiled_func(0x710470851cULL);
    totk::recompiler::register_recompiled_func(0x710470851cULL, [](Arm64Context *c) {
        auto &memory = totk::runtime::MemoryManager::instance();
        if (c->x[0] == 0 || !is_mapped_guest_range(memory, c->x[0], sizeof(uint64_t)) ||
            c->x[2] == 0 || !is_mapped_guest_range(memory, c->x[2], sizeof(uint64_t))) {
            c->pc = c->x[30];
            return;
        }
        if (s_orig_710470851c) {
            s_orig_710470851c(c);
        } else {
            c->pc = c->x[30];
        }
    });

    static totk::recompiler::RecompiledFunc s_orig_7104708640 =
        totk::recompiler::lookup_recompiled_func(0x7104708640ULL);
    totk::recompiler::register_recompiled_func(0x7104708640ULL, [](Arm64Context *c) {
        auto &memory = totk::runtime::MemoryManager::instance();
        if (c->x[0] == 0 || !is_mapped_guest_range(memory, c->x[0], sizeof(uint64_t)) ||
            c->x[2] == 0 || !is_mapped_guest_range(memory, c->x[2], sizeof(uint64_t))) {
            c->pc = c->x[30];
            return;
        }
        if (s_orig_7104708640) {
            s_orig_7104708640(c);
        } else {
            c->pc = c->x[30];
        }
    });

    static totk::recompiler::RecompiledFunc s_orig_7104708704 =
        totk::recompiler::lookup_recompiled_func(0x7104708704ULL);
    totk::recompiler::register_recompiled_func(0x7104708704ULL, [](Arm64Context *c) {
        auto &memory = totk::runtime::MemoryManager::instance();
        if (c->x[0] == 0 || !is_mapped_guest_range(memory, c->x[0], sizeof(uint64_t)) ||
            c->x[2] == 0 || !is_mapped_guest_range(memory, c->x[2], sizeof(uint64_t))) {
            c->pc = c->x[30];
            return;
        }
        if (s_orig_7104708704) {
            s_orig_7104708704(c);
        } else {
            c->pc = c->x[30];
        }
    });

    static totk::recompiler::RecompiledFunc s_orig_7104708784 =
        totk::recompiler::lookup_recompiled_func(0x7104708784ULL);
    totk::recompiler::register_recompiled_func(0x7104708784ULL, [](Arm64Context *c) {
        auto &memory = totk::runtime::MemoryManager::instance();
        if (c->x[0] == 0 || !is_mapped_guest_range(memory, c->x[0], sizeof(uint64_t)) ||
            c->x[2] == 0 || !is_mapped_guest_range(memory, c->x[2], sizeof(uint64_t))) {
            c->pc = c->x[30];
            return;
        }
        if (s_orig_7104708784) {
            s_orig_7104708784(c);
        } else {
            c->pc = c->x[30];
        }
    });

    static totk::recompiler::RecompiledFunc s_orig_71047088b0 =
        totk::recompiler::lookup_recompiled_func(0x71047088b0ULL);
    totk::recompiler::register_recompiled_func(0x71047088b0ULL, [](Arm64Context *c) {
        auto &memory = totk::runtime::MemoryManager::instance();
        if (c->x[0] == 0 || !is_mapped_guest_range(memory, c->x[0], sizeof(uint64_t)) ||
            c->x[2] == 0 || !is_mapped_guest_range(memory, c->x[2], sizeof(uint64_t))) {
            c->pc = c->x[30];
            return;
        }
        if (s_orig_71047088b0) {
            s_orig_71047088b0(c);
        } else {
            c->pc = c->x[30];
        }
    });

    // Guard f_7104707ef0 against invalid object and heap pointers.
    static totk::recompiler::RecompiledFunc s_orig_7104707ef0 =
        totk::recompiler::lookup_recompiled_func(0x7104707ef0ULL);
    totk::recompiler::register_recompiled_func(0x7104707ef0ULL, [](Arm64Context *c) {
        constexpr uint64_t guest_base = totk::runtime::GUEST_MEMORY_BASE;
        constexpr uint64_t guest_limit = totk::runtime::GUEST_MEMORY_LIMIT;
        auto &memory = totk::runtime::MemoryManager::instance();
        auto valid_guest_range = [&memory](uint64_t address, uint64_t size) {
            return memory.get_guest_base() != nullptr &&
                   address >= guest_base && address <= guest_limit &&
                   size <= guest_limit - address &&
                   memory.commit(address, static_cast<size_t>(size));
        };

        if (c->x[0] == 0 || !valid_guest_range(c->x[0], 0x1c) ||
            !valid_guest_range(c->x[2], sizeof(uint64_t))) {
            c->pc = c->x[30];
            return;
        }

        if (s_orig_7104707ef0) {
            s_orig_7104707ef0(c);
        } else {
            c->pc = c->x[30];
        }
    });

    // Guard f_7104708100 against invalid object and heap pointers.
    static totk::recompiler::RecompiledFunc s_orig_7104708100 =
        totk::recompiler::lookup_recompiled_func(0x7104708100ULL);
    totk::recompiler::register_recompiled_func(0x7104708100ULL, [](Arm64Context *c) {
        constexpr uint64_t guest_base = totk::runtime::GUEST_MEMORY_BASE;
        constexpr uint64_t guest_limit = totk::runtime::GUEST_MEMORY_LIMIT;
        auto &memory = totk::runtime::MemoryManager::instance();
        auto valid_guest_range = [&memory](uint64_t address, uint64_t size) {
            return memory.get_guest_base() != nullptr &&
                   address >= guest_base && address <= guest_limit &&
                   size <= guest_limit - address &&
                   memory.commit(address, static_cast<size_t>(size));
        };

        if (c->x[0] == 0 || !valid_guest_range(c->x[0], 0x1c) ||
            !valid_guest_range(c->x[2], sizeof(uint64_t))) {
            c->pc = c->x[30];
            return;
        }

        if (s_orig_7104708100) {
            s_orig_7104708100(c);
        } else {
            c->pc = c->x[30];
        }
    });

    // Guard f_7104707ff0 against invalid container and heap pointers.
    static totk::recompiler::RecompiledFunc s_orig_7104707ff0 =
        totk::recompiler::lookup_recompiled_func(0x7104707ff0ULL);
    totk::recompiler::register_recompiled_func(0x7104707ff0ULL, [](Arm64Context *c) {
        constexpr uint64_t guest_base = totk::runtime::GUEST_MEMORY_BASE;
        constexpr uint64_t guest_limit = totk::runtime::GUEST_MEMORY_LIMIT;
        constexpr uint64_t max_elements = 0x100000;
        auto &memory = totk::runtime::MemoryManager::instance();
        auto valid_guest_range = [&memory](uint64_t address, uint64_t size) {
            return address >= guest_base && address <= guest_limit &&
                   size <= guest_limit - address &&
                   memory.commit(address, static_cast<size_t>(size));
        };

        const bool invalid_container = !valid_guest_range(c->x[0], 0x78);
        const bool invalid_heap = !valid_guest_range(c->x[2], sizeof(uint64_t));
        if (invalid_container || invalid_heap || !s_orig_7104707ff0) {
            if (invalid_container) {
                c->x[0] = hlr_bump_alloc(0x80, 0x10);
            }
            if (invalid_heap) {
                c->x[2] = get_default_heap();
            }

            c->x[1] = std::min(c->x[1], max_elements);
            const uint64_t safe_buf =
                hlr_bump_alloc(std::max<uint64_t>(c->x[1], 1) * sizeof(uint64_t), 0x10);
            *reinterpret_cast<uint64_t *>(c->x[0] + 0x68) = c->x[1];
            *reinterpret_cast<uint64_t *>(c->x[0] + 0x70) = safe_buf;
            c->pc = c->x[30];
            return;
        }

        s_orig_7104707ff0(c);
    });

    // Guard f_7104708180 against invalid container and heap pointers.
    static totk::recompiler::RecompiledFunc s_orig_7104708180 =
        totk::recompiler::lookup_recompiled_func(0x7104708180ULL);
    totk::recompiler::register_recompiled_func(0x7104708180ULL, [](Arm64Context *c) {
        constexpr uint64_t guest_base = totk::runtime::GUEST_MEMORY_BASE;
        constexpr uint64_t guest_limit = totk::runtime::GUEST_MEMORY_LIMIT;
        constexpr uint64_t max_elements = 0x100000;
        auto &memory = totk::runtime::MemoryManager::instance();
        auto valid_guest_range = [&memory](uint64_t address, uint64_t size) {
            return memory.get_guest_base() != nullptr &&
                   address >= guest_base && address <= guest_limit &&
                   size <= guest_limit - address &&
                   memory.commit(address, static_cast<size_t>(size));
        };

        const bool invalid_container = !valid_guest_range(c->x[0], 0x48);
        const bool invalid_heap = !valid_guest_range(c->x[2], sizeof(uint64_t));
        if (invalid_container || invalid_heap || !s_orig_7104708180) {
            if (invalid_container) {
                c->x[0] = hlr_bump_alloc(0x50, 0x10);
            }
            if (invalid_heap) {
                c->x[2] = get_default_heap();
            }

            c->x[1] = std::min(c->x[1], max_elements);
            const uint64_t safe_buf =
                hlr_bump_alloc(std::max<uint64_t>(c->x[1], 1) * sizeof(uint64_t), 0x10);
            *reinterpret_cast<uint64_t *>(c->x[0] + 0x38) = c->x[1];
            *reinterpret_cast<uint64_t *>(c->x[0] + 0x40) = safe_buf;
            c->pc = c->x[30];
            return;
        }

        s_orig_7104708180(c);
    });

    // Skip SEAD table setup when its container is null or outside guest memory.
    static totk::recompiler::RecompiledFunc s_orig_7104707f6c =
        totk::recompiler::lookup_recompiled_func(0x7104707f6cULL);
    totk::recompiler::register_recompiled_func(0x7104707f6cULL, [](Arm64Context *c) {
        auto &memory = totk::runtime::MemoryManager::instance();
        if (c->x[0] == 0 || memory.get_guest_base() == nullptr ||
            !memory.is_in_guest_range(c->x[0]) ||
            !memory.commit(c->x[0], sizeof(uint64_t))) {
            c->pc = c->x[30];
            return;
        }

        if (s_orig_7104707f6c) {
            s_orig_7104707f6c(c);
        } else {
            c->pc = c->x[30];
        }
    });

    static totk::recompiler::RecompiledFunc s_orig_7104708290 =
        totk::recompiler::lookup_recompiled_func(0x7104708290ULL);
    totk::recompiler::register_recompiled_func(0x7104708290ULL, [](Arm64Context *c) {
        auto &memory = totk::runtime::MemoryManager::instance();
        if (c->x[0] == 0 || memory.get_guest_base() == nullptr ||
            !memory.is_in_guest_range(c->x[0]) ||
            !memory.commit(c->x[0], sizeof(uint64_t))) {
            if (c->x[22] == 0) {
                c->x[22] = get_safe_container_node();
            }
            c->pc = c->x[30];
            return;
        }

        if (s_orig_7104708290) {
            s_orig_7104708290(c);
        } else {
            c->pc = c->x[30];
        }
    });

    // Bypass f_7104cceae8 to avoid dereferencing a null lookup result.
    totk::recompiler::register_recompiled_func(0x7104cceae8ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Bypass f_7104ccdcd4 to avoid dereferencing a null lookup result.
    totk::recompiler::register_recompiled_func(0x7104ccdcd4ULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
        return;
    });

    static totk::recompiler::RecompiledFunc s_orig_71047083a0 =
        totk::recompiler::lookup_recompiled_func(0x71047083a0ULL);
    totk::recompiler::register_recompiled_func(0x71047083a0ULL, [](Arm64Context *c) {
        auto &memory = totk::runtime::MemoryManager::instance();
        if (c->x[0] == 0 || memory.get_guest_base() == nullptr ||
            !memory.is_in_guest_range(c->x[0]) ||
            !memory.commit(c->x[0], sizeof(uint64_t))) {
            c->pc = c->x[30];
            return;
        }

        if (s_orig_71047083a0) {
            s_orig_71047083a0(c);
        } else {
            c->pc = c->x[30];
        }
    });

    // Guard f_71046f038c (agl layer setup)
    totk::recompiler::register_recompiled_func(0x71046f038cULL, [](Arm64Context *c) {
        std::printf("  [HLR Hook] f_71046f038c (agl layer setup) -> clean return\n");
        c->pc = c->x[30];
    });

    // Guard f_71046ee3cc (list lookup: null list returns null)
    static totk::recompiler::RecompiledFunc s_orig_71046ee3cc =
        totk::recompiler::lookup_recompiled_func(0x71046ee3ccULL);
    totk::recompiler::register_recompiled_func(0x71046ee3ccULL, [](Arm64Context *c) {
        if (c->x[0] == 0) {
            c->x[0] = 0;
            c->pc = c->x[30];
            return;
        }
        if (s_orig_71046ee3cc) {
            s_orig_71046ee3cc(c);
        } else {
            c->x[0] = 0;
            c->pc = c->x[30];
        }
    });

    // Universal operator new / delete hooks
    auto hook_op_new = [](uint64_t addr) {
        totk::recompiler::register_recompiled_func(addr, [](Arm64Context *c) {
            uint64_t sz = c->x[0];
            if (sz == 0) sz = 8;
            if (sz > 0x40000000ULL) sz = 0x1000;
            c->x[0] = hlr_bump_alloc(sz, 0x10);
            c->pc = c->x[30];
        });
    };
    auto hook_op_delete = [](uint64_t addr) {
        totk::recompiler::register_recompiled_func(addr, [](Arm64Context *c) {
            c->pc = c->x[30];
        });
    };

    hook_op_new(0x7104b10bb0ULL); // main operator new(unsigned long)
    hook_op_new(0x7105046b60ULL); // main operator new[](unsigned long)
    hook_op_new(0x71067ba2c0ULL); // main operator new(unsigned long, nothrow)
    hook_op_new(0x71067ba2e0ULL); // main operator new[](unsigned long, nothrow)
    hook_op_new(0x71067ba300ULL); // main operator new(unsigned long, align)
    hook_op_new(0x71067ba320ULL); // main operator new[](unsigned long, align)
    hook_op_new(0x710c63d6f0ULL); // sdk operator new(unsigned long)
    hook_op_new(0x710c63d760ULL); // sdk operator new(unsigned long, nothrow)
    hook_op_new(0x710c63d790ULL); // sdk operator new[](unsigned long)
    hook_op_new(0x710c63d7a0ULL); // sdk operator new[](unsigned long, nothrow)
    hook_op_new(0x710c63d830ULL); // sdk operator new(unsigned long, align)
    hook_op_new(0x710c63d900ULL); // sdk operator new[](unsigned long, align)

    hook_op_delete(0x7104167e20ULL); // main operator delete(void*)
    hook_op_delete(0x7104342c40ULL); // main operator delete[](void*)
    hook_op_delete(0x71067ba340ULL); // main operator delete(void*, nothrow)
    hook_op_delete(0x71067ba350ULL); // main operator delete[](void*, nothrow)
    hook_op_delete(0x71067ba360ULL); // main operator delete(void*, align)
    hook_op_delete(0x71067ba370ULL); // main operator delete[](void*, align)
    hook_op_delete(0x710c63d7d0ULL); // sdk operator delete(void*)
    hook_op_delete(0x710c63d7e0ULL); // sdk operator delete(void*, nothrow)
    hook_op_delete(0x710c63d7f0ULL); // sdk operator delete(void*, unsigned long)
    hook_op_delete(0x710c63d800ULL); // sdk operator delete[](void*)
    hook_op_delete(0x710c63d810ULL); // sdk operator delete[](void*, nothrow)
    hook_op_delete(0x710c63d820ULL); // sdk operator delete[](void*, unsigned long)
    hook_op_delete(0x710c63d940ULL); // sdk operator delete(void*, align)
    hook_op_delete(0x710c63d950ULL); // sdk operator delete(void*, align, nothrow)
    hook_op_delete(0x710c63d960ULL); // sdk operator delete(void*, unsigned long, align)
    hook_op_delete(0x710c63d970ULL); // sdk operator delete[](void*, align)
    hook_op_delete(0x710c63d980ULL); // sdk operator delete[](void*, align, nothrow)
    hook_op_delete(0x710c63d990ULL); // sdk operator delete[](void*, unsigned long, align)

    // Clamped memset implementations to prevent underflow crashes
    auto hook_memset = [](uint64_t addr) {
        totk::recompiler::register_recompiled_func(addr, [](Arm64Context *c) {
            uint64_t dst = c->x[0];
            int val = static_cast<int>(c->x[1] & 0xff);
            uint64_t sz = c->x[2];
            if (dst >= 0x7000000000ULL && dst < 0x7300000000ULL && sz > 0) {
                if (sz > 0x10000000ULL) sz = 0x10000;
                std::memset(reinterpret_cast<void *>(dst), val, sz);
            }
            c->x[0] = dst;
            c->pc = c->x[30];
        });
    };
    hook_memset(0x710c5840b0ULL); // sdk memset
    hook_memset(0x7106bb7b80ULL); // main PLT memset

    // Guard f_7104b4f180 (allocator block reset / setup)
    totk::recompiler::register_recompiled_func(0x7104b4f180ULL, [](Arm64Context *c) {
        uint64_t obj = c->x[0];
        if (obj >= 0x7000000000ULL && obj < 0x7300000000ULL) {
            uint64_t *p_start = reinterpret_cast<uint64_t *>(obj + 0x1ed0);
            uint64_t *p_mid   = reinterpret_cast<uint64_t *>(obj + 0x1ed8);
            uint64_t *p_end   = reinterpret_cast<uint64_t *>(obj + 0x1ee0);
            if (*p_end >= 0x7300000000ULL || *p_end < 0x7000000000ULL) {
                *p_start = obj + 0x1f00;
                *p_mid   = obj + 0x1f00;
                *p_end   = obj + 0x2000;
            }
            if (*p_start >= 0x7000000000ULL && *p_start < 0x7300000000ULL) {
                *reinterpret_cast<uint32_t *>(*p_start) = 0xd15ab1ed;
            }
            if (*p_mid >= 0x7000000000ULL && *p_mid < 0x7300000000ULL) {
                *reinterpret_cast<uint32_t *>(*p_mid) = 0xad000d1e;
            }
            if (*p_end >= 0x7000000000ULL && *p_end < 0x7300000000ULL) {
                *reinterpret_cast<uint32_t *>(*p_end) = 0xad000d1e;
            }
        }
        std::printf("  [HLR Hook] f_7104b4f180 (allocator block setup obj=0x%llx) -> clean return\n",
                    static_cast<unsigned long long>(obj));
        std::fflush(stdout);
        c->x[0] = obj + 8;
        c->pc = c->x[30];
    });

    // Guard f_7104b4f548 (graphics context & driver setup)
    static totk::recompiler::RecompiledFunc s_orig_7104b4f548 =
        totk::recompiler::lookup_recompiled_func(0x7104b4f548ULL);
    totk::recompiler::register_recompiled_func(0x7104b4f548ULL, [](Arm64Context *c) {
        uint64_t def_heap = get_default_heap();
        uint64_t *p_target = reinterpret_cast<uint64_t *>(0x7107a2cad0ULL);
        if (*p_target == 0) {
            *p_target = *reinterpret_cast<uint64_t *>(def_heap);
            *reinterpret_cast<uint64_t *>(0x7107a2cad0ULL + 0x8) = def_heap;
        }
        if (s_orig_7104b4f548) {
            s_orig_7104b4f548(c);
        } else {
            c->pc = c->x[30];
        }
    });

    // Guard f_71051739b0 (graphics/context setup object constructor)
    static totk::recompiler::RecompiledFunc s_orig_71051739b0 =
        totk::recompiler::lookup_recompiled_func(0x71051739b0ULL);
    totk::recompiler::register_recompiled_func(0x71051739b0ULL, [](Arm64Context *c) {
        if (c->x[0] == 0) {
            c->x[0] = hlr_bump_alloc(0x2000, 0x10);
            std::printf("  [HLR Hook] f_71051739b0 fallback allocated this=0x%llx\n",
                        static_cast<unsigned long long>(c->x[0]));
        }
        if (c->x[1] == 0 || *reinterpret_cast<uint64_t *>(c->x[1]) == 0) {
            c->x[1] = get_default_heap();
        }
        *reinterpret_cast<uint64_t *>(0x7107a27000ULL + 0xcb8) = c->x[0];
        *reinterpret_cast<uint64_t *>(0x7107a27000ULL + 0xcb8 + 8) = c->x[1];

        if (s_orig_71051739b0) {
            s_orig_71051739b0(c);
        } else {
            c->pc = c->x[30];
        }
    });

    // Guard f_7106a37c4c (internal heap block allocator fallback)
    static totk::recompiler::RecompiledFunc s_orig_7106a37c4c =
        totk::recompiler::lookup_recompiled_func(0x7106a37c4cULL);
    totk::recompiler::register_recompiled_func(0x7106a37c4cULL, [](Arm64Context *c) {
        if (s_orig_7106a37c4c) {
            s_orig_7106a37c4c(c);
        }
        if (c->x[0] == 0) {
            uint64_t req_sz = 0x1000;
            if (c->x[1] >= 0x7000000000ULL && c->x[1] < 0x7300000000ULL) {
                req_sz = *reinterpret_cast<uint64_t *>(c->x[1]);
                if (req_sz == 0 || req_sz > 0x10000000ULL) req_sz = 0x1000;
            } else if (c->x[1] > 0 && c->x[1] <= 0x10000000ULL) {
                req_sz = c->x[1];
            }
            c->x[0] = hlr_bump_alloc(req_sz, 0x10);
            std::printf("  [HLR Hook] f_7106a37c4c fallback allocated sz=0x%llx -> 0x%llx (from LR=0x%llx)\n",
                        static_cast<unsigned long long>(req_sz),
                        static_cast<unsigned long long>(c->x[0]),
                        static_cast<unsigned long long>(c->x[30]));
            std::fflush(stdout);
        }
    });

    // Guard f_7105137990 (32-byte struct array quicksort -> null-safe return)
    totk::recompiler::register_recompiled_func(0x7105137990ULL, [](Arm64Context *c) {
        std::printf("  [HLR Hook] f_7105137990 (array sort arr=0x%llx start=%d end=%d) -> clean return\n",
                    static_cast<unsigned long long>(c->x[0]),
                    static_cast<int>(c->x[1]),
                    static_cast<int>(c->x[2]));
        std::fflush(stdout);
        c->pc = c->x[30];
    });

    // Guard f_7105134f60 (cache manager update/prune -> clean return)
    totk::recompiler::register_recompiled_func(0x7105134f60ULL, [](Arm64Context *c) {
        std::printf("  [HLR Hook] f_7105134f60 (cache manager update mgr=0x%llx) -> clean return\n",
                    static_cast<unsigned long long>(c->x[0]));
        std::fflush(stdout);
        c->pc = c->x[30];
    });

    // Bypass SEAD table lookups that can dereference an absent or invalid table.
    auto bypass_sead_table_lookup = [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
        return;
    };
    totk::recompiler::register_recompiled_func(0x7104ccb13cULL, bypass_sead_table_lookup);
    totk::recompiler::register_recompiled_func(0x7104ccb2acULL, bypass_sead_table_lookup);
    totk::recompiler::register_recompiled_func(0x7104ccb43cULL, bypass_sead_table_lookup);
    totk::recompiler::register_recompiled_func(0x7104ccb600ULL, bypass_sead_table_lookup);
    totk::recompiler::register_recompiled_func(0x7104ccb844ULL, bypass_sead_table_lookup);
    totk::recompiler::register_recompiled_func(0x7104ccbaacULL, bypass_sead_table_lookup);
    totk::recompiler::register_recompiled_func(0x7104ccbd5cULL, bypass_sead_table_lookup);

    // Guard f_7104cca050 (string table lookup)
    static totk::recompiler::RecompiledFunc s_orig_7104cca050 =
        totk::recompiler::lookup_recompiled_func(0x7104cca050ULL);
    totk::recompiler::register_recompiled_func(0x7104cca050ULL, [](Arm64Context *c) {
        constexpr uint64_t object_size = 0x2bc;
        constexpr uint64_t table_offset = 0x2b0;
        constexpr uint32_t max_entries = 128;
        constexpr uint64_t guest_base = totk::runtime::GUEST_MEMORY_BASE;
        constexpr uint64_t guest_limit = totk::runtime::GUEST_MEMORY_LIMIT;
        auto &memory = totk::runtime::MemoryManager::instance();

        auto valid_range = [&memory](uint64_t address, uint64_t size) {
            return address >= guest_base && address <= guest_limit &&
                   size <= guest_limit - address &&
                   memory.commit(address, static_cast<size_t>(size));
        };
        auto clear_outputs = [&c, &valid_range]() {
            for (uint64_t output : {c->x[1], c->x[2]}) {
                if (output != 0 && valid_range(output, sizeof(uint64_t))) {
                    *reinterpret_cast<uint64_t *>(output) = 0;
                }
            }
        };
        auto return_empty = [&c, &clear_outputs]() {
            clear_outputs();
            c->pc = c->x[30];
        };

        const uint64_t object = c->x[0];
        if (c->x[1] == 0 || c->x[2] == 0 ||
            !valid_range(c->x[1], sizeof(uint64_t)) ||
            !valid_range(c->x[2], sizeof(uint64_t)) ||
            !valid_range(object, object_size)) {
            return_empty();
            return;
        }

        auto *count_ptr = reinterpret_cast<uint32_t *>(object + 0x2b8);
        *count_ptr = std::min<uint32_t>(*count_ptr, max_entries);
        const uint32_t count = *count_ptr;
        const uint64_t table = *reinterpret_cast<uint64_t *>(object + table_offset);

        if (table == 0 ||
            !valid_range(table, static_cast<uint64_t>(count) * sizeof(uint64_t))) {
            return_empty();
            return;
        }
        for (uint32_t i = 0; i < count; ++i) {
            const uint64_t string = *reinterpret_cast<uint64_t *>(table + i * sizeof(uint64_t));
            if (string == 0 || !valid_range(string, 1)) {
                return_empty();
                return;
            }
        }

        if (s_orig_7104cca050) {
            s_orig_7104cca050(c);
        } else {
            return_empty();
        }
    });

    // Guard f_7105128a40 (node lookup in descriptor tree -> safe empty node)
    static totk::recompiler::RecompiledFunc s_orig_7105128a40 =
        totk::recompiler::lookup_recompiled_func(0x7105128a40ULL);
    totk::recompiler::register_recompiled_func(0x7105128a40ULL, [](Arm64Context *c) {
        static uint64_t s_empty_node = 0;
        if (s_empty_node == 0) {
            s_empty_node = hlr_bump_alloc(0x200, 0x20);
            std::memset(reinterpret_cast<void *>(s_empty_node), 0, 0x200);
        }
        if (c->x[0] == 0) {
            c->x[0] = s_empty_node;
            c->pc = c->x[30];
            return;
        }
        if (s_orig_7105128a40) {
            s_orig_7105128a40(c);
        }
        if (c->x[0] == 0) {
            c->x[0] = s_empty_node;
        }
        c->pc = c->x[30];
    });

    // Guard f_71047ec6a4 (material / shader property instance creation)
    static totk::recompiler::RecompiledFunc s_orig_71047ec6a4 =
        totk::recompiler::lookup_recompiled_func(0x71047ec6a4ULL);
    totk::recompiler::register_recompiled_func(0x71047ec6a4ULL, [](Arm64Context *c) {
        static uint64_t s_dummy_inst = 0;
        if (s_dummy_inst == 0) {
            s_dummy_inst = hlr_bump_alloc(0x200, 0x40);
            uint64_t sub = hlr_bump_alloc(0x200, 0x40);
            *reinterpret_cast<uint64_t *>(s_dummy_inst + 0x18) = sub;
            uint64_t sub_sub = hlr_bump_alloc(0x200, 0x40);
            *reinterpret_cast<uint64_t *>(sub + 0x48) = sub_sub;
            *reinterpret_cast<uint32_t *>(sub_sub + 0xe0) = 0;
            uint64_t sub_c0 = hlr_bump_alloc(0x100, 0x20);
            *reinterpret_cast<uint32_t *>(sub_c0 + 0x10) = 0;
            *reinterpret_cast<uint64_t *>(s_dummy_inst + 0xc0) = sub_c0;
        }
        *reinterpret_cast<uint64_t *>(0x7107ad27c8ULL) = s_dummy_inst;

        if (s_orig_71047ec6a4) {
            s_orig_71047ec6a4(c);
        } else {
            c->pc = c->x[30];
        }
    });

    // Guard f_7104a623dc (SEAD resource / event registration)
    totk::recompiler::register_recompiled_func(0x7104a623dcULL, [](Arm64Context *c) {
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Guard f_7104a24ed4 (heap/allocator property query)
    totk::recompiler::register_recompiled_func(0x7104a24ed4ULL, [](Arm64Context *c) {
        if (c->x[0] >= 0x7000000000ULL && c->x[0] < 0x7300000000ULL) {
            uint64_t p = *reinterpret_cast<uint64_t *>(c->x[0] + 0xc0);
            if (p >= 0x7000000000ULL && p < 0x7300000000ULL) {
                c->x[0] = *reinterpret_cast<uint32_t *>(p + 0x10);
            } else {
                c->x[0] = 0;
            }
        } else {
            c->x[0] = 0;
        }
        c->pc = c->x[30];
    });

    // Guard f_710499ca78 (log buffer flush / string termination -> clean return 1)
    totk::recompiler::register_recompiled_func(0x710499ca78ULL, [](Arm64Context *c) {
        c->x[0] = 1;
        c->pc = c->x[30];
    });

    // Guard f_71043784dc (resource relocation check & dispatch)
    totk::recompiler::register_recompiled_func(0x71043784dcULL, [](Arm64Context *c) {
        std::printf("  [HLR Hook] f_71043784dc (resource relocation) res=0x%llx -> marked relocated\n",
                    static_cast<unsigned long long>(c->x[0]));
        std::fflush(stdout);
        c->pc = c->x[30];
    });

    // Guard nn::util::RelocationTable::Relocate (0x710c081aa0) and PLT 0x7106bb7bb0
    auto hook_reloc_table = [](uint64_t addr) {
        totk::recompiler::register_recompiled_func(addr, [](Arm64Context *c) {
            uint64_t table = c->x[0];
            if (table >= 0x7000000000ULL && table < 0x7300000000ULL) {
                uint32_t count = *reinterpret_cast<uint32_t *>(table + 8);
                if (count > 50000) {
                    std::printf("  [HLR Hook] RelocationTable::Relocate(0x%llx) clamped count %u -> clean return\n",
                                static_cast<unsigned long long>(table), count);
                    std::fflush(stdout);
                    c->pc = c->x[30];
                    return;
                }
            }
            static totk::recompiler::RecompiledFunc s_orig_reloc =
                totk::recompiler::lookup_recompiled_func(0x710c081aa0ULL);
            if (s_orig_reloc && table >= 0x7000000000ULL) {
                s_orig_reloc(c);
            } else {
                c->pc = c->x[30];
            }
        });
    };
    hook_reloc_table(0x710c081aa0ULL);
    hook_reloc_table(0x7106bb7bb0ULL);

    // Guard f_71050e35c0 (instance initialized check)
    totk::recompiler::register_recompiled_func(0x71050e35c0ULL, [](Arm64Context *c) {
        if (c->x[0] < 0x7000000000ULL || c->x[0] >= 0x7300000000ULL) {
            c->x[0] = 0;
            c->pc = c->x[30];
            return;
        }
        uint64_t p1 = *reinterpret_cast<uint64_t *>(c->x[0] + 0x28);
        if (p1 < 0x7000000000ULL || p1 >= 0x7300000000ULL) {
            c->x[0] = 0;
            c->pc = c->x[30];
            return;
        }
        uint64_t p2 = *reinterpret_cast<uint64_t *>(p1 + 0x20);
        if (p2 < 0x7000000000ULL || p2 >= 0x7300000000ULL) {
            c->x[0] = 0;
            c->pc = c->x[30];
            return;
        }
        c->x[0] = (*reinterpret_cast<uint8_t *>(p2 + 0x110) != 0) ? 1 : 0;
        c->pc = c->x[30];
    });

    // Guard f_71050e35f0 (instance query)
    totk::recompiler::register_recompiled_func(0x71050e35f0ULL, [](Arm64Context *c) {
        if (c->x[0] < 0x7000000000ULL || c->x[0] >= 0x7300000000ULL) {
            c->x[0] = 1;
        } else {
            uint64_t p = *reinterpret_cast<uint64_t *>(c->x[0] + 0x68);
            if (p >= 0x7000000000ULL && p < 0x7300000000ULL) {
                uint8_t b = *reinterpret_cast<uint8_t *>(p);
                c->x[0] = (b != 0) ? 1 : 0;
            } else {
                c->x[0] = 0;
            }
        }
        c->pc = c->x[30];
        return;
    });

    // Guard f_71050e3700 against invalid pointers in its object chains.
    static totk::recompiler::RecompiledFunc s_orig_71050e3700 =
        totk::recompiler::lookup_recompiled_func(0x71050e3700ULL);
    totk::recompiler::register_recompiled_func(0x71050e3700ULL, [](Arm64Context *c) {
        constexpr uint64_t guest_base = 0x7000000000ULL;
        constexpr uint64_t guest_limit = 0x7300000000ULL;
        auto valid_range = [](uint64_t addr, uint64_t size) {
            return addr >= guest_base && size <= guest_limit - addr &&
                   addr <= guest_limit - size;
        };
        auto fail = [c]() {
            c->x[0] = 0;
            c->pc = c->x[30];
        };

        if (c->sp < guest_base + 48 || !valid_range(c->sp - 48, 48) ||
            !valid_range(c->x[0], 1) || !valid_range(c->x[1], 1)) {
            fail();
            return;
        }

        constexpr uint64_t table_slot = 0x710796df40ULL;
        if (!valid_range(table_slot, sizeof(uint64_t))) {
            fail();
            return;
        }
        uint64_t table = *reinterpret_cast<const uint64_t *>(table_slot);
        if (!valid_range(table, 3 * sizeof(uint64_t))) {
            fail();
            return;
        }

        for (uint64_t i = 0; i < 3; ++i) {
            uint64_t offset = *reinterpret_cast<const uint64_t *>(table + i * sizeof(uint64_t));
            if (offset > guest_limit - c->x[1] ||
                !valid_range(c->x[1] + offset, sizeof(uint64_t))) {
                fail();
                return;
            }

            uint64_t object = *reinterpret_cast<const uint64_t *>(c->x[1] + offset);
            if (object == 0 && i < 2) continue;
            if (!valid_range(object, 1) ||
                !valid_range(object + 0x68, sizeof(uint64_t))) {
                fail();
                return;
            }
            uint64_t state = *reinterpret_cast<const uint64_t *>(object + 0x68);
            if (!valid_range(state, 1)) {
                fail();
                return;
            }
        }

        if (s_orig_71050e3700) s_orig_71050e3700(c);
        else fail();
    });

    // Initialize the format block flag for f_710437bb60
    totk::recompiler::register_recompiled_func(0x710437bb60ULL, [](Arm64Context *c) {
        if (c->x[0] >= 0x7000000000ULL && c->x[0] < 0x7300000000ULL) {
            *reinterpret_cast<uint8_t *>(c->x[0] + 0x110) = 1;
        }
        c->x[0] = 0;
        c->pc = c->x[30];
    });

    // Initialize the format block flag for f_71042c7744
    totk::recompiler::register_recompiled_func(0x71042c7744ULL, [](Arm64Context *c) {
        if (c->x[0] >= 0x7000000000ULL && c->x[0] < 0x7300000000ULL) {
            *reinterpret_cast<uint8_t *>(c->x[0] + 0x110) = 1;
        }
        c->pc = c->x[30];
    });

    static totk::recompiler::RecompiledFunc s_orig_7104d0c788 =
        totk::recompiler::lookup_recompiled_func(0x7104d0c788ULL);
    totk::recompiler::register_recompiled_func(0x7104d0c788ULL, [](Arm64Context *c) {
        if (c->x[0] < 0x7000000000ULL || c->x[0] >= 0x7300000000ULL - 0x20) {
            c->pc = c->x[30];
            return;
        }
        if (c->x[1] < 0x7000000000ULL || c->x[1] >= 0x7300000000ULL - 0x20) {
            c->x[1] = hlr_bump_alloc(0x100, 0x10);
        }
        if (c->x[0] == c->x[1]) {
            c->pc = c->x[30];
            return;
        }
        if (s_orig_7104d0c788) s_orig_7104d0c788(c);
        else c->pc = c->x[30];
    });

    // Guard nn::util::BinaryFileHeader::GetFirstBlock against invalid object pointers
    static totk::recompiler::RecompiledFunc s_orig_get_first_block =
        totk::recompiler::lookup_recompiled_func(0x7106bb7b00ULL);
    totk::recompiler::register_recompiled_func(0x7106bb7b00ULL, [](Arm64Context *c) {
        static uint64_t s_safe_first_block = 0;
        if (s_safe_first_block == 0) {
            s_safe_first_block = hlr_bump_alloc(0x1000, 0x20);
            *reinterpret_cast<uint64_t *>(s_safe_first_block + 0x20) = s_safe_first_block + 0x200;
            *reinterpret_cast<uint8_t *>(s_safe_first_block + 0x110) = 1;
        }
        if (c->x[0] < 0x7000000000ULL || c->x[0] >= 0x7300000000ULL) {
            c->x[0] = s_safe_first_block;
            c->pc = c->x[30];
            return;
        }
        if (s_orig_get_first_block) s_orig_get_first_block(c);
        else c->x[0] = s_safe_first_block;

        if (c->x[0] < 0x7000000000ULL || c->x[0] >= 0x7300000000ULL) {
            c->x[0] = s_safe_first_block;
        }
        uint64_t data_ptr = *reinterpret_cast<uint64_t *>(c->x[0] + 0x20);
        if (data_ptr < 0x7000000000ULL || data_ptr >= 0x7300000000ULL) {
            data_ptr = hlr_bump_alloc(0x1000, 0x20);
            *reinterpret_cast<uint64_t *>(c->x[0] + 0x20) = data_ptr;
        }
        *reinterpret_cast<uint8_t *>(c->x[0] + 0x110) = 1;
        c->pc = c->x[30];
    });
}

void sead_matrix34f_mult(const sead::Matrix34f &m, const sead::Vector3f &v, sead::Vector3f &out) {
    // Row 0
    __m128 r0 = _mm_set_ps(m.m[0][3], m.m[0][2], m.m[0][1], m.m[0][0]);
    // Row 1
    __m128 r1 = _mm_set_ps(m.m[1][3], m.m[1][2], m.m[1][1], m.m[1][0]);
    // Row 2
    __m128 r2 = _mm_set_ps(m.m[2][3], m.m[2][2], m.m[2][1], m.m[2][0]);

    __m128 v_vec = _mm_set_ps(1.0f, v.z, v.y, v.x);

    __m128 prod0 = _mm_mul_ps(r0, v_vec);
    __m128 prod1 = _mm_mul_ps(r1, v_vec);
    __m128 prod2 = _mm_mul_ps(r2, v_vec);

    // Horizontal adds for each row
    __m128 hsum0 = _mm_hadd_ps(prod0, prod0);
    hsum0 = _mm_hadd_ps(hsum0, hsum0);

    __m128 hsum1 = _mm_hadd_ps(prod1, prod1);
    hsum1 = _mm_hadd_ps(hsum1, hsum1);

    __m128 hsum2 = _mm_hadd_ps(prod2, prod2);
    hsum2 = _mm_hadd_ps(hsum2, hsum2);

    out.x = _mm_cvtss_f32(hsum0);
    out.y = _mm_cvtss_f32(hsum1);
    out.z = _mm_cvtss_f32(hsum2);
}

void sead_vector3f_normalize(sead::Vector3f &v) {
    float len_sq = v.x * v.x + v.y * v.y + v.z * v.z;
    if (len_sq <= 1e-12f) return;

    __m128 s = _mm_set_ss(len_sq);
    __m128 inv = _mm_rsqrt_ss(s);
    float inv_len = _mm_cvtss_f32(inv);

    // One iteration of Newton-Raphson refinement
    inv_len = 0.5f * inv_len * (3.0f - len_sq * inv_len * inv_len);

    v.x *= inv_len;
    v.y *= inv_len;
    v.z *= inv_len;
}

uint32_t sead_calc_crc32(const void *data, size_t size) {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    uint32_t crc = ~0u;

    // Process 8-byte blocks via SSE4.2 / x86_64 crc32
    while (size >= 8) {
        uint64_t val;
        __builtin_memcpy(&val, p, 8);
        crc = static_cast<uint32_t>(_mm_crc32_u64(crc, val));
        p += 8;
        size -= 8;
    }

    while (size > 0) {
        crc = _mm_crc32_u8(crc, *p);
        p++;
        size--;
    }

    return ~crc;
}

} // namespace totk::hlr
