#pragma once

#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <unordered_map>

namespace totk::runtime {

enum Result : uint32_t {
    ResultSuccess = 0,
    ResultTimeout = 0xEA01,
    ResultCancelled = 0xEC01,
    ResultInvalidHandle = 0xE401,
    ResultInvalidAddress = 0xCC01,
};

class KernelHLE {
public:
    static KernelHLE &instance();

    // Horizon SVC system call implementations
    uint32_t svc_arbitrate_lock(uint32_t thread_handle, uint64_t lock_addr, uint32_t tag);
    uint32_t svc_arbitrate_unlock(uint64_t lock_addr);
    uint32_t svc_wait_process_wide_key_atomic(uint64_t key_addr, uint64_t mutex_addr, uint32_t tag, int64_t timeout_ns);
    uint32_t svc_signal_process_wide_key(uint64_t key_addr, int32_t count);

    uint32_t svc_sleep_thread(int64_t nano_seconds);

private:
    KernelHLE();
    ~KernelHLE();

    std::mutex global_mutex_;
    std::unordered_map<uint64_t, std::condition_variable> condvar_map_;
};

} // namespace totk::runtime
