#pragma once

#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <string>
#include <unordered_map>
#include <vector>

namespace totk::runtime {

enum Result : uint32_t {
    ResultSuccess = 0,
    ResultTimeout = 0xEA01,
    ResultCancelled = 0xEC01,
    ResultInvalidHandle = 0xE401,
    ResultInvalidAddress = 0xCC01,
};

struct SvcTraceEntry {
    std::string svc_name;
    uint32_t svc_id;
    uint64_t arg0;
    uint64_t arg1;
    uint32_t result;
    uint64_t timestamp_ns;
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
    uint32_t svc_get_thread_id(uint64_t *thread_id_out, uint32_t thread_handle);
    uint32_t svc_connect_to_named_port(uint32_t *handle_out, const char *port_name);
    uint32_t svc_send_sync_request(uint32_t session_handle);
    uint32_t svc_close_handle(uint32_t handle);

    // Tracing for --boot-trace
    const std::vector<SvcTraceEntry> &get_trace_log() const { return m_trace_log; }
    void clear_trace_log() { m_trace_log.clear(); }

private:
    KernelHLE();
    ~KernelHLE();

    std::mutex global_mutex_;
    std::unordered_map<uint64_t, std::condition_variable> condvar_map_;
    std::vector<SvcTraceEntry> m_trace_log;
};

} // namespace totk::runtime
