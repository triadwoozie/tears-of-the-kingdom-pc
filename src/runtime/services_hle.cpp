#include "services_hle.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace totk::runtime {

enum ServiceKind {
    KIND_NONE = 0,
    KIND_SM = 1,
    KIND_LM,
    KIND_LOGGER,
    KIND_APM,
    KIND_APM_SESSION,
    KIND_APPLET,
    KIND_APP_PROXY,
    KIND_COMMON_STATE,
    KIND_SELF_CONTROLLER,
    KIND_WINDOW_CONTROLLER,
    KIND_AUDIO_CONTROLLER,
    KIND_DISPLAY_CONTROLLER,
    KIND_AOC,
    KIND_FS,
    KIND_TIME,
    KIND_USER_CLOCK,
    KIND_STEADY_CLOCK,
    KIND_ROM_STORAGE,
    KIND_ACCOUNT,
    KIND_ACCOUNT_PROFILE,
    KIND_FONTS,
    KIND_AUD_MANAGER,
    KIND_AUD_RENDERER,
    KIND_AUD_DEVICE,
    KIND_SAVE_FS,
    KIND_VI,
    KIND_NVDRV,
};

static const char *s_kind_names[] = {
    "none", "sm:", "lm", "logger", "apm", "apm-session",
    "appletOE", "application-proxy", "common-state", "self-controller",
    "window-controller", "audio-controller", "display-controller",
    "aoc:u", "fsp-srv", "time:u", "user-clock", "steady-clock",
    "rom-storage", "acc:u0", "account-profile", "pl:u",
    "audren:u", "audio-renderer", "audio-device", "save-filesystem",
    "vi:u", "nvdrv"
};

struct SessionObject {
    uint32_t handle = 0;
    ServiceKind kind = KIND_NONE;
    bool is_domain = false;
    uint32_t domain_objects[16]{};
    ServiceKind domain_kinds[16]{};
};

static std::mutex s_services_mutex;
static std::unordered_map<uint32_t, SessionObject> s_sessions;
static uint32_t s_next_handle = 0x10000;

static uint32_t allocate_session(ServiceKind kind) {
    uint32_t h = s_next_handle++;
    SessionObject obj{};
    obj.handle = h;
    obj.kind = kind;
    s_sessions[h] = obj;
    return h;
}

ServicesHLE &ServicesHLE::instance() {
    static ServicesHLE s_instance;
    return s_instance;
}

ServicesHLE::ServicesHLE() = default;
ServicesHLE::~ServicesHLE() = default;

bool ServicesHLE::initialize() {
    std::lock_guard<std::mutex> lock(s_services_mutex);
    s_sessions.clear();
    s_next_handle = 0x10000;
    m_trace_log.clear();
    return true;
}

void ServicesHLE::shutdown() {
    std::lock_guard<std::mutex> lock(s_services_mutex);
    s_sessions.clear();
}

bool ServicesHLE::connect_port(const std::string &port_name, uint32_t *handle) {
    if (!handle) return false;
    std::lock_guard<std::mutex> lock(s_services_mutex);

    if (port_name == "sm:" || port_name == "sm") {
        *handle = allocate_session(KIND_SM);
        return true;
    }

    return false;
}

bool ServicesHLE::close_session(uint32_t handle) {
    std::lock_guard<std::mutex> lock(s_services_mutex);
    return s_sessions.erase(handle) > 0;
}

static bool write_cmif_reply(uint8_t *bytes, size_t size, uint32_t result_code,
                             const void *data = nullptr, size_t data_len = 0,
                             uint32_t move_handle = 0) {
    size_t header_size = move_handle ? 16 : 8;
    size_t aligned_header = (header_size + 15) & ~15ULL;
    size_t total_reply_len = aligned_header + 16 + data_len;
    if (total_reply_len > size) return false;

    std::memset(bytes, 0, total_reply_len);

    // Hipc reply header
    uint32_t *u32 = reinterpret_cast<uint32_t *>(bytes);
    u32[0] = 0; // Type 0 (normal response)
    u32[1] = static_cast<uint32_t>((total_reply_len - header_size) / 4);

    if (move_handle) {
        u32[1] |= 0x80000000; // Has handle descriptor
        u32[2] = 0x20;         // 1 move handle
        u32[3] = move_handle;
    }

    // CMIF magic SFCO (0x4F434653)
    uint32_t *cmif = reinterpret_cast<uint32_t *>(bytes + aligned_header);
    cmif[0] = 0x4F434653; // 'SFCO'
    cmif[1] = 0;          // version / reserved
    cmif[2] = result_code;
    cmif[3] = 0;          // token / reserved

    if (data && data_len > 0) {
        std::memcpy(bytes + aligned_header + 16, data, data_len);
    }

    return true;
}

uint32_t ServicesHLE::dispatch_ipc(uint32_t handle, void *buffer, size_t size) {
    if (!buffer || size < 16) return 0xE401; // InvalidHandle / message

    std::lock_guard<std::mutex> lock(s_services_mutex);
    auto it = s_sessions.find(handle);
    if (it == s_sessions.end()) {
        return 0xE401;
    }

    SessionObject &session = it->second;
    uint8_t *bytes = reinterpret_cast<uint8_t *>(buffer);
    uint32_t *u32 = reinterpret_cast<uint32_t *>(bytes);

    uint32_t h0 = u32[0];
    uint32_t h1 = u32[1];
    uint32_t type = h0 & 0xFFFF;

    // Type 2 / 15: Close session
    if (type == 2 || type == 15) {
        s_sessions.erase(handle);
        return 0;
    }

    // Locate CMIF header
    size_t header_len = (h1 >> 31) ? 16 : 8;
    size_t aligned_cmif = (header_len + 15) & ~15ULL;
    if (aligned_cmif + 16 > size) return 0x1C01;

    uint32_t *cmif = reinterpret_cast<uint32_t *>(bytes + aligned_cmif);
    uint32_t magic = cmif[0];
    uint32_t cmd_id = cmif[2];

    // Check SFCI (0x49434653)
    if (magic != 0x49434653) {
        // Control or Tipc command fallback
        cmd_id = type >= 16 ? type - 16 : 0;
    }

    const uint8_t *payload = bytes + aligned_cmif + 16;
    size_t payload_len = size - (aligned_cmif + 16);

    uint32_t result_code = 0;
    uint32_t new_handle = 0;

    // Trace logging
    auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    IpcTraceEntry trace_entry{
        s_kind_names[session.kind],
        cmd_id,
        handle,
        0,
        static_cast<uint64_t>(now_ns)
    };

    switch (session.kind) {
        case KIND_SM: {
            if (cmd_id == 0) {
                // Initialize(pid)
                result_code = 0;
            } else if (cmd_id == 1) {
                // GetService(name: 8 bytes)
                char svc_name[9] = {0};
                if (payload_len >= 8) {
                    std::memcpy(svc_name, payload, 8);
                }
                std::string name_str(svc_name);

                ServiceKind target_kind = KIND_NONE;
                if (name_str.rfind("fsp-srv", 0) == 0) target_kind = KIND_FS;
                else if (name_str.rfind("set", 0) == 0) target_kind = KIND_NONE; // fallback
                else if (name_str.rfind("time:u", 0) == 0) target_kind = KIND_TIME;
                else if (name_str.rfind("acc:u0", 0) == 0) target_kind = KIND_ACCOUNT;
                else if (name_str.rfind("apm", 0) == 0) target_kind = KIND_APM;
                else if (name_str.rfind("appletOE", 0) == 0) target_kind = KIND_APPLET;
                else if (name_str.rfind("audren:u", 0) == 0) target_kind = KIND_AUD_MANAGER;
                else if (name_str.rfind("pl:u", 0) == 0) target_kind = KIND_FONTS;
                else if (name_str.rfind("vi:u", 0) == 0) target_kind = KIND_VI;
                else if (name_str.rfind("nvdrv", 0) == 0) target_kind = KIND_NVDRV;

                new_handle = allocate_session(target_kind != KIND_NONE ? target_kind : KIND_SM);
                result_code = 0;
            }
            break;
        }

        case KIND_APPLET: {
            if (cmd_id == 0) {
                // OpenApplicationProxy
                new_handle = allocate_session(KIND_APP_PROXY);
                result_code = 0;
            }
            break;
        }

        case KIND_APP_PROXY: {
            if (cmd_id == 0) {
                // GetCommonStateGetter
                new_handle = allocate_session(KIND_COMMON_STATE);
                result_code = 0;
            } else if (cmd_id == 1) {
                // GetSelfController
                new_handle = allocate_session(KIND_SELF_CONTROLLER);
                result_code = 0;
            } else if (cmd_id == 2) {
                // GetWindowController
                new_handle = allocate_session(KIND_WINDOW_CONTROLLER);
                result_code = 0;
            } else if (cmd_id == 3) {
                // GetAudioController
                new_handle = allocate_session(KIND_AUDIO_CONTROLLER);
                result_code = 0;
            } else if (cmd_id == 4) {
                // GetDisplayController
                new_handle = allocate_session(KIND_DISPLAY_CONTROLLER);
                result_code = 0;
            }
            break;
        }

        case KIND_FS: {
            if (cmd_id == 1) {
                // SetCurrentProcess
                result_code = 0;
            } else if (cmd_id == 11) {
                // OpenRomFs
                new_handle = allocate_session(KIND_ROM_STORAGE);
                result_code = 0;
            } else if (cmd_id == 18) {
                // OpenSaveDataFileSystem
                new_handle = allocate_session(KIND_SAVE_FS);
                result_code = 0;
            }
            break;
        }

        case KIND_TIME: {
            if (cmd_id == 0) {
                // GetStandardUserSystemClock
                new_handle = allocate_session(KIND_USER_CLOCK);
                result_code = 0;
            } else if (cmd_id == 2) {
                // GetStandardSteadyClock
                new_handle = allocate_session(KIND_STEADY_CLOCK);
                result_code = 0;
            }
            break;
        }

        case KIND_ACCOUNT: {
            if (cmd_id == 0) {
                // GetUserCount -> returns 1
                uint32_t count = 1;
                write_cmif_reply(bytes, size, 0, &count, sizeof(count));
                trace_entry.result_code = 0;
                m_trace_log.push_back(trace_entry);
                return 0;
            } else if (cmd_id == 1) {
                // GetUserExistence -> true
                uint32_t exists = 1;
                write_cmif_reply(bytes, size, 0, &exists, sizeof(exists));
                trace_entry.result_code = 0;
                m_trace_log.push_back(trace_entry);
                return 0;
            } else if (cmd_id == 3) {
                // GetProfile
                new_handle = allocate_session(KIND_ACCOUNT_PROFILE);
                result_code = 0;
            }
            break;
        }

        case KIND_AUD_MANAGER: {
            if (cmd_id == 0) {
                // OpenAudioRenderer
                new_handle = allocate_session(KIND_AUD_RENDERER);
                result_code = 0;
            } else if (cmd_id == 1) {
                // GetAudioRendererWorkBufferSize
                uint64_t work_sz = 1024 * 1024; // 1 MiB work buffer
                write_cmif_reply(bytes, size, 0, &work_sz, sizeof(work_sz));
                trace_entry.result_code = 0;
                m_trace_log.push_back(trace_entry);
                return 0;
            }
            break;
        }

        default:
            result_code = 0;
            break;
    }

    trace_entry.result_code = result_code;
    m_trace_log.push_back(trace_entry);

    write_cmif_reply(bytes, size, result_code, nullptr, 0, new_handle);
    return result_code;
}

} // namespace totk::runtime
