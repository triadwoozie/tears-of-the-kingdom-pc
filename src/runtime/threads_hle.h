#pragma once

#include <cstdint>
#include <functional>

namespace totk::runtime {

extern thread_local uint8_t *t_guest_tls;

class ThreadManager {
public:
    static ThreadManager &instance();

    uint32_t create_thread(uint64_t entry_point, uint64_t arg, uint64_t stack_top, int32_t priority, int32_t core_id);
    void start_thread(uint32_t thread_handle);
    void exit_thread();

    uint8_t *get_current_tls() const { return t_guest_tls; }
    void set_current_tls(uint8_t *tls) { t_guest_tls = tls; }

private:
    ThreadManager();
    ~ThreadManager();
};

} // namespace totk::runtime
