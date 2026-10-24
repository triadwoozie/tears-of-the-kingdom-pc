#pragma once

#include <cstdint>
#include <string>

namespace totk::runtime {

class ServiceManager {
public:
    static ServiceManager &instance();

    bool initialize();
    uint32_t dispatch_ipc(const std::string &service_name, uint32_t command_id, void *cmif_buffer, size_t buffer_size);

private:
    ServiceManager();
    ~ServiceManager();
};

} // namespace totk::runtime
