#include "kernel_hle.h"
#include <chrono>
#include <thread>

namespace totk::runtime {

KernelHLE &KernelHLE::instance() {
    static KernelHLE k;
    return k;
}

KernelHLE::KernelHLE() = default;
KernelHLE::~KernelHLE() = default;

uint32_t KernelHLE::svc_arbitrate_lock(uint32_t thread_handle, uint64_t lock_addr, uint32_t tag) {
    (void)thread_handle; (void)tag;
    if (!lock_addr) return ResultInvalidAddress;

    std::unique_lock<std::mutex> lock(global_mutex_);
    uint32_t *mutex_val = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(lock_addr));

    uint32_t expected = 0;
    while (!__atomic_compare_exchange_n(mutex_val, &expected, tag, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        condvar_map_[lock_addr].wait(lock);
        expected = 0;
    }
    return ResultSuccess;
}

uint32_t KernelHLE::svc_arbitrate_unlock(uint64_t lock_addr) {
    if (!lock_addr) return ResultInvalidAddress;

    std::unique_lock<std::mutex> lock(global_mutex_);
    uint32_t *mutex_val = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(lock_addr));
    __atomic_store_n(mutex_val, 0, __ATOMIC_RELEASE);

    auto it = condvar_map_.find(lock_addr);
    if (it != condvar_map_.end()) {
        it->second.notify_one();
    }
    return ResultSuccess;
}

uint32_t KernelHLE::svc_wait_process_wide_key_atomic(uint64_t key_addr, uint64_t mutex_addr, uint32_t tag, int64_t timeout_ns) {
    if (!key_addr || !mutex_addr) return ResultInvalidAddress;

    std::unique_lock<std::mutex> lock(global_mutex_);
    // Release the user mutex
    uint32_t *mutex_val = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(mutex_addr));
    __atomic_store_n(mutex_val, 0, __ATOMIC_RELEASE);

    auto &cv = condvar_map_[key_addr];
    auto &mutex_cv = condvar_map_[mutex_addr];
    mutex_cv.notify_one();

    if (timeout_ns < 0) {
        cv.wait(lock);
    } else {
        auto status = cv.wait_for(lock, std::chrono::nanoseconds(timeout_ns));
        if (status == std::cv_status::timeout) {
            // Re-acquire user mutex before returning
            uint32_t expected = 0;
            while (!__atomic_compare_exchange_n(mutex_val, &expected, tag, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
                mutex_cv.wait(lock);
                expected = 0;
            }
            return ResultTimeout;
        }
    }

    // Re-acquire user mutex
    uint32_t expected = 0;
    while (!__atomic_compare_exchange_n(mutex_val, &expected, tag, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        mutex_cv.wait(lock);
        expected = 0;
    }

    return ResultSuccess;
}

uint32_t KernelHLE::svc_signal_process_wide_key(uint64_t key_addr, int32_t count) {
    if (!key_addr) return ResultInvalidAddress;

    std::unique_lock<std::mutex> lock(global_mutex_);
    auto it = condvar_map_.find(key_addr);
    if (it != condvar_map_.end()) {
        if (count == 1) {
            it->second.notify_one();
        } else {
            it->second.notify_all();
        }
    }
    return ResultSuccess;
}

uint32_t KernelHLE::svc_sleep_thread(int64_t nano_seconds) {
    if (nano_seconds <= 0) {
        std::this_thread::yield();
    } else {
        std::this_thread::sleep_for(std::chrono::nanoseconds(nano_seconds));
    }
    return ResultSuccess;
}

} // namespace totk::runtime
