#include "threads_hle.h"
#include <cstdlib>
#include <thread>
#include <vector>

namespace totk::runtime {

thread_local uint8_t *t_guest_tls = nullptr;

struct ThreadControlBlock {
    uint32_t handle;
    uint64_t entry_point;
    uint64_t arg;
    uint64_t stack_top;
    uint8_t *tls_memory;
    std::thread host_thread;
};

static std::vector<ThreadControlBlock *> g_threads;
static uint32_t g_next_handle = 0x100;

ThreadManager &ThreadManager::instance() {
    static ThreadManager tm;
    return tm;
}

ThreadManager::ThreadManager() = default;
ThreadManager::~ThreadManager() = default;

uint32_t ThreadManager::create_thread(uint64_t entry_point, uint64_t arg, uint64_t stack_top, int32_t priority, int32_t core_id) {
    (void)priority; (void)core_id;

    auto *tcb = new ThreadControlBlock();
    tcb->handle = g_next_handle++;
    tcb->entry_point = entry_point;
    tcb->arg = arg;
    tcb->stack_top = stack_top;
    tcb->tls_memory = static_cast<uint8_t *>(std::calloc(1, 0x1000)); // 4KB TLS page

    g_threads.push_back(tcb);
    return tcb->handle;
}

void ThreadManager::start_thread(uint32_t thread_handle) {
    for (auto *tcb : g_threads) {
        if (tcb->handle == thread_handle) {
            tcb->host_thread = std::thread([tcb]() {
                t_guest_tls = tcb->tls_memory;
                // Entry point execution hook
            });
            break;
        }
    }
}

void ThreadManager::exit_thread() {
    // Thread self-exit
}

} // namespace totk::runtime
