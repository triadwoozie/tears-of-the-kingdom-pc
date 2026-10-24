#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace totk::runtime {

struct IpcTraceEntry {
    std::string service_name;
    uint32_t command_id;
    uint32_t handle;
    uint32_t result_code;
    uint64_t timestamp_ns;
};

class ServicesHLE {
public:
    static ServicesHLE &instance();

    bool initialize();
    void shutdown();

    // Connect to named service port (e.g. "sm:")
    bool connect_port(const std::string &port_name, uint32_t *handle);

    // Close service session handle
    bool close_session(uint32_t handle);

    // Process incoming CMIF / Hipc request buffer in-place
    uint32_t dispatch_ipc(uint32_t handle, void *buffer, size_t size);

    // RomFS and SaveData paths configuration
    void set_romfs_path(const std::string &path) { m_romfs_path = path; }
    void set_save_dir(const std::string &path) { m_save_dir = path; }

    // Trace logging for --boot-trace
    const std::vector<IpcTraceEntry> &get_trace_log() const { return m_trace_log; }
    void clear_trace_log() { m_trace_log.clear(); }

private:
    ServicesHLE();
    ~ServicesHLE();

    std::string m_romfs_path{"local/romfs.bin"};
    std::string m_save_dir{"work/save"};
    std::vector<IpcTraceEntry> m_trace_log;
};

} // namespace totk::runtime
