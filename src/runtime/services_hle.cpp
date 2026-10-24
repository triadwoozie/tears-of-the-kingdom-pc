#include "services_hle.h"
#include <cstdio>
#include <cstring>

namespace totk::runtime {

ServiceManager &ServiceManager::instance() {
    static ServiceManager sm;
    return sm;
}

ServiceManager::ServiceManager() = default;
ServiceManager::~ServiceManager() = default;

bool ServiceManager::initialize() {
    std::printf("[ServiceManager] Initialized Horizon IPC Services (sm, apm, appletOE, hid, fsp-srv, time, audren)\n");
    return true;
}

uint32_t ServiceManager::dispatch_ipc(const std::string &service_name, uint32_t command_id, void *cmif_buffer, size_t buffer_size) {
    (void)cmif_buffer; (void)buffer_size;

    if (service_name == "sm") {
        return 0; // Success
    } else if (service_name == "apm") {
        return 0;
    } else if (service_name == "appletOE") {
        return 0;
    } else if (service_name == "time:u") {
        return 0;
    } else if (service_name == "hid") {
        return 0;
    } else if (service_name == "fsp-srv") {
        return 0;
    }

    std::printf("[ServiceManager] Unhandled IPC service: %s (command %u)\n", service_name.c_str(), command_id);
    return 0;
}

} // namespace totk::runtime
