#include "kernel_hle.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>

int main() {
    std::printf("[Test] Testing Horizon Kernel Synchronization Primitives...\n");

    auto &kernel = totk::runtime::KernelHLE::instance();

    // 1. Test svc_arbitrate_lock and svc_arbitrate_unlock
    uint32_t lock_word = 0;
    uint64_t lock_addr = reinterpret_cast<uintptr_t>(&lock_word);

    uint32_t res = kernel.svc_arbitrate_lock(1, lock_addr, 0x10);
    assert(res == totk::runtime::ResultSuccess);
    assert(lock_word == 0x10);

    res = kernel.svc_arbitrate_unlock(lock_addr);
    assert(res == totk::runtime::ResultSuccess);
    assert(lock_word == 0);

    // 2. Test multi-threaded lock contention
    std::atomic<int> counter{0};
    constexpr int NUM_THREADS = 4;
    constexpr int ITERS_PER_THREAD = 1000;

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back([&kernel, lock_addr, &counter, t]() {
            for (int i = 0; i < ITERS_PER_THREAD; i++) {
                kernel.svc_arbitrate_lock(t + 1, lock_addr, 0x100 + t);
                counter.fetch_add(1, std::memory_order_relaxed);
                kernel.svc_arbitrate_unlock(lock_addr);
            }
        });
    }

    for (auto &th : threads) {
        th.join();
    }
    assert(counter.load() == NUM_THREADS * ITERS_PER_THREAD);

    // 3. Test svc_wait_process_wide_key_atomic & svc_signal_process_wide_key
    uint32_t key_word = 0;
    uint64_t key_addr = reinterpret_cast<uintptr_t>(&key_word);

    std::atomic<bool> signaled{false};
    std::thread waiter([&kernel, key_addr, lock_addr, &signaled]() {
        kernel.svc_arbitrate_lock(2, lock_addr, 0x20);
        uint32_t wait_res = kernel.svc_wait_process_wide_key_atomic(key_addr, lock_addr, 0x20, -1);
        assert(wait_res == totk::runtime::ResultSuccess);
        signaled = true;
        kernel.svc_arbitrate_unlock(lock_addr);
    });

    // Give waiter time to enter wait
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    kernel.svc_signal_process_wide_key(key_addr, 1);
    waiter.join();
    assert(signaled.load());

    std::printf("[Test] Horizon Kernel Synchronization verified successfully!\n");
    return 0;
}
