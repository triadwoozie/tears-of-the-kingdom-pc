#include "kernel_hle.h"
#include "services_hle.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace totk::runtime {

KernelHLE &KernelHLE::instance() {
    static KernelHLE k;
    return k;
}

KernelHLE::KernelHLE() = default;
KernelHLE::~KernelHLE() = default;

static uint64_t get_time_ns() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

uint32_t KernelHLE::svc_arbitrate_lock(uint32_t thread_handle, uint64_t lock_addr, uint32_t tag) {
    (void)thread_handle;
    std::unique_lock<std::mutex> lock(global_mutex_);
    if (lock_addr) {
        uint32_t *ptr = reinterpret_cast<uint32_t *>(lock_addr);
        std::condition_variable &cv = condvar_map_[lock_addr];
        while (*ptr != 0) {
            cv.wait(lock);
        }
        *ptr = tag;
    }
    m_trace_log.push_back({"svcArbitrateLock", 0x1A, lock_addr, tag, ResultSuccess, get_time_ns()});
    return ResultSuccess;
}

uint32_t KernelHLE::svc_arbitrate_unlock(uint64_t lock_addr) {
    std::unique_lock<std::mutex> lock(global_mutex_);
    if (lock_addr) {
        *reinterpret_cast<uint32_t *>(lock_addr) = 0;
    }
    auto it = condvar_map_.find(lock_addr);
    if (it != condvar_map_.end()) {
        it->second.notify_one();
    }
    m_trace_log.push_back({"svcArbitrateUnlock", 0x1B, lock_addr, 0, ResultSuccess, get_time_ns()});
    return ResultSuccess;
}

uint32_t KernelHLE::svc_wait_process_wide_key_atomic(uint64_t key_addr, uint64_t mutex_addr, uint32_t tag, int64_t timeout_ns) {
    std::unique_lock<std::mutex> lock(global_mutex_);
    if (mutex_addr) {
        *reinterpret_cast<uint32_t *>(mutex_addr) = 0;
        auto it_mutex = condvar_map_.find(mutex_addr);
        if (it_mutex != condvar_map_.end()) {
            it_mutex->second.notify_one();
        }
    }

    std::condition_variable &cv = condvar_map_[key_addr];
    uint32_t res = ResultSuccess;

    if (timeout_ns < 0) {
        cv.wait(lock);
    } else if (timeout_ns == 0) {
        res = ResultTimeout;
    } else {
        auto status = cv.wait_for(lock, std::chrono::nanoseconds(timeout_ns));
        if (status == std::cv_status::timeout) {
            res = ResultTimeout;
        }
    }

    if (mutex_addr) {
        uint32_t *ptr = reinterpret_cast<uint32_t *>(mutex_addr);
        std::condition_variable &cv_mutex = condvar_map_[mutex_addr];
        while (*ptr != 0) {
            cv_mutex.wait(lock);
        }
        *ptr = tag;
    }
    m_trace_log.push_back({"svcWaitProcessWideKeyAtomic", 0x1C, key_addr, mutex_addr, res, get_time_ns()});
    return res;
}

uint32_t KernelHLE::svc_signal_process_wide_key(uint64_t key_addr, int32_t count) {
    std::unique_lock<std::mutex> lock(global_mutex_);
    auto it = condvar_map_.find(key_addr);
    if (it != condvar_map_.end()) {
        if (count == 1) {
            it->second.notify_one();
        } else if (count < 0 || count > 1) {
            it->second.notify_all();
        }
    }
    m_trace_log.push_back({"svcSignalProcessWideKey", 0x1D, key_addr, static_cast<uint64_t>(count), ResultSuccess, get_time_ns()});
    return ResultSuccess;
}

uint32_t KernelHLE::svc_sleep_thread(int64_t nano_seconds) {
    if (nano_seconds > 0) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(nano_seconds));
    } else if (nano_seconds == 0) {
        std::this_thread::yield();
    }
    return ResultSuccess;
}

uint32_t KernelHLE::svc_get_thread_id(uint64_t *thread_id_out, uint32_t thread_handle) {
    if (!thread_id_out) return ResultInvalidAddress;
    *thread_id_out = (thread_handle == 0xFFFF8000) ? 1 : thread_handle;
    return ResultSuccess;
}

uint32_t KernelHLE::svc_connect_to_named_port(uint32_t *handle_out, const char *port_name) {
    if (!handle_out || !port_name) return ResultInvalidAddress;
    bool ok = ServicesHLE::instance().connect_port(port_name, handle_out);
    uint32_t res = ok ? ResultSuccess : ResultInvalidHandle;
    m_trace_log.push_back({"svcConnectToNamedPort", 0x1F, reinterpret_cast<uint64_t>(port_name), 0, res, get_time_ns()});
    return res;
}

uint32_t KernelHLE::svc_send_sync_request(uint32_t session_handle) {
    // In TotK, thread TLS contains the IPC command buffer at TLS offset 0x00
    // Services dispatcher unmarshalls and processes the command in-place
    uint8_t mock_ipc_buf[0x100]{};
    uint32_t res = ServicesHLE::instance().dispatch_ipc(session_handle, mock_ipc_buf, sizeof(mock_ipc_buf));
    m_trace_log.push_back({"svcSendSyncRequest", 0x21, session_handle, 0, res, get_time_ns()});
    return res;
}

uint32_t KernelHLE::svc_close_handle(uint32_t handle) {
    ServicesHLE::instance().close_session(handle);
    m_trace_log.push_back({"svcCloseHandle", 0x16, handle, 0, ResultSuccess, get_time_ns()});
    return ResultSuccess;
}

} // namespace totk::runtime
