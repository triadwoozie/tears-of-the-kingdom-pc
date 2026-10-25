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
    KIND_PCTL,
    KIND_PCTL_SERVICE,
    KIND_APPLICATION_FUNCTIONS,
    KIND_LAUNCH_STORAGE,
    KIND_LAUNCH_ACCESSOR,
    KIND_ACCOUNT_MANAGER,
};

static const char *s_kind_names[] = {
    "none", "sm:", "lm", "logger", "apm", "apm-session",
    "appletOE", "application-proxy", "common-state", "self-controller",
    "window-controller", "audio-controller", "display-controller",
    "aoc:u", "fsp-srv", "time:u", "user-clock", "steady-clock",
    "rom-storage", "acc:u0", "account-profile", "pl:u",
    "audren:u", "audio-renderer", "audio-device", "save-filesystem",
    "vi:u", "nvdrv", "pctl", "pctl-service",
    "application-functions", "launch-storage", "launch-accessor", "account-manager"
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
    if (port_name == "apm") {
        *handle = allocate_session(KIND_APM);
        return true;
    }
    if (port_name == "fsp-srv") {
        *handle = allocate_session(KIND_FS);
        return true;
    }
    if (port_name == "vi:u") {
        *handle = allocate_session(KIND_VI);
        return true;
    }
    if (port_name == "nvdrv") {
        *handle = allocate_session(KIND_NVDRV);
        return true;
    }
    if (port_name == "time:u") {
        *handle = allocate_session(KIND_TIME);
        return true;
    }
    if (port_name == "audren:u") {
        *handle = allocate_session(KIND_AUD_MANAGER);
        return true;
    }

    return false;
}

bool ServicesHLE::close_session(uint32_t handle) {
    std::lock_guard<std::mutex> lock(s_services_mutex);
    return s_sessions.erase(handle) > 0;
}

static bool write_reply(uint8_t *bytes, size_t size, bool tipc, uint32_t token, uint32_t moved,
                        bool domain, uint32_t object, const void *payload = nullptr, size_t length = 0,
                        uint32_t result_code = 0) {
    size_t raw = moved ? 16 : 8;
    size_t header = tipc ? raw : (raw + 15) & ~15ULL;
    size_t body = tipc ? 4 : 16;
    size_t domain_header = domain ? 16 : 0;
    size_t end = (header + domain_header + body + length + (object ? 4 : 0) + 3) & ~3ULL;
    if (end > size) return false;

    uint32_t out[64]{0};
    out[0] = 0;
    out[1] = static_cast<uint32_t>((end - raw) / 4) | (moved ? 0x80000000 : 0);
    if (moved) {
        out[2] = 0x20;
        out[3] = moved;
    }
    if (domain) {
        out[header / 4] = object ? 1 : 0;
        header += domain_header;
    }
    if (!tipc) {
        out[header / 4] = 0x4F434653; // 'SFCO'
        out[header / 4 + 1] = 0;
        out[header / 4 + 2] = result_code;
        out[header / 4 + 3] = token;
    } else {
        out[header / 4] = result_code;
    }
    if (payload && length > 0) {
        std::memcpy(reinterpret_cast<uint8_t *>(out) + header + body, payload, length);
    }
    if (object) {
        std::memcpy(reinterpret_cast<uint8_t *>(out) + header + body + length, &object, 4);
    }
    std::memcpy(bytes, out, end);
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

    bool tipc = (type >= 16);
    bool control = (type == 5 || type == 7);
    bool domain = session.is_domain && !control && !tipc;

    uint32_t cmd_id = 0;
    uint32_t token = 0;
    const uint8_t *payload = nullptr;
    size_t payload_len = 0;
    size_t aligned_cmif = 0;

    if (tipc) {
        cmd_id = type - 16;
        size_t raw = (h1 >> 31) ? 16 : 8;
        payload = bytes + raw;
        payload_len = (size > raw) ? size - raw : 0;
    } else {
        // Locate CMIF header by scanning for magic SFCI (0x49434653)
        for (size_t p = 8; p + 16 <= size; p += 4) {
            if (*reinterpret_cast<const uint32_t *>(bytes + p) == 0x49434653) {
                aligned_cmif = p;
                break;
            }
        }
        if (aligned_cmif == 0) {
            size_t header_len = (h1 >> 31) ? 16 : 8;
            aligned_cmif = (header_len + 15) & ~15ULL;
        }
        if (aligned_cmif + 16 > size) return 0x1C01;

        uint32_t *cmif = reinterpret_cast<uint32_t *>(bytes + aligned_cmif);
        cmd_id = cmif[2];
        token = cmif[3];
        payload = bytes + aligned_cmif + 16;
        payload_len = (size > aligned_cmif + 16) ? size - (aligned_cmif + 16) : 0;
    }

    // Auto-detect domain request if an explicit domain header precedes SFCI
    if (!control && !tipc && aligned_cmif >= 32) {
        size_t dom_offset = aligned_cmif - 16;
        uint32_t d0 = *reinterpret_cast<const uint32_t *>(bytes + dom_offset);
        uint32_t obj = *reinterpret_cast<const uint32_t *>(bytes + dom_offset + 4);
        if ((d0 & 0xFFFF) == 1 && obj >= 1 && obj <= 16) {
            domain = true;
            session.is_domain = true;
            if (session.domain_kinds[obj - 1] == KIND_NONE) {
                session.domain_kinds[obj - 1] = session.kind;
                session.domain_objects[obj - 1] = obj;
            }
        }
    }

    if (control && cmd_id == 0) { // ConvertSessionToDomain
        session.is_domain = true;
        session.domain_kinds[0] = session.kind;
        session.domain_objects[0] = 1;
        uint32_t root = 1;
        write_reply(bytes, size, false, token, 0, false, 0, &root, sizeof(root), 0);
        return 0;
    }
    if (control && cmd_id == 3) { // QueryPointerBufferSize
        uint16_t ptr_sz = (session.kind == KIND_FS || session.kind == KIND_SAVE_FS || session.kind == KIND_TIME) ? 0x1000 : 0;
        write_reply(bytes, size, false, token, 0, false, 0, &ptr_sz, sizeof(ptr_sz), 0);
        return 0;
    }
    if (control && (cmd_id == 2 || cmd_id == 4)) { // DuplicateSession
        uint32_t clone = allocate_session(session.kind);
        write_reply(bytes, size, false, token, clone, false, 0, nullptr, 0, 0);
        return 0;
    }

    ServiceKind current_kind = session.kind;
    if (domain) {
        size_t dom_offset = (aligned_cmif >= 16) ? aligned_cmif - 16 : 16;
        uint32_t target_object = *reinterpret_cast<uint32_t *>(bytes + dom_offset + 4);
        if (target_object >= 1 && target_object <= 16) {
            current_kind = session.domain_kinds[target_object - 1];
        }
    }

    uint32_t result_code = 0;
    uint32_t new_handle = 0;
    ServiceKind open_kind = KIND_NONE;
    const void *out_data = nullptr;
    size_t out_data_len = 0;

    // Trace logging
    auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    IpcTraceEntry trace_entry{
        s_kind_names[current_kind],
        cmd_id,
        handle,
        0,
        static_cast<uint64_t>(now_ns)
    };

    std::printf("  [IPC Dispatch] handle=0x%x kind=%s cmd=%u ctrl=%d dom=%d (raw cmif: 0x%zx)\n",
                handle, s_kind_names[current_kind], cmd_id, control, domain, aligned_cmif);
    if (current_kind == KIND_FS) {
        std::printf("    [FS IPC Dump] bytes:");
        for (size_t i = 0; i < 64 && i < size; ++i) {
            std::printf(" %02x", bytes[i]);
            if ((i + 1) % 16 == 0) std::printf("\n                 ");
        }
        std::printf("\n");
    }

    switch (current_kind) {
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
                std::printf("  [IPC SM] GetService(\"%s\") [raw payload:", name_str.c_str());
                for (size_t i = 0; i < 16; ++i) {
                    std::printf(" %02x", payload[i]);
                }
                std::printf("]\n");

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
                else if (name_str.rfind("lm", 0) == 0) target_kind = KIND_LM;
                else if (name_str.rfind("pctl", 0) == 0) target_kind = KIND_PCTL;
                else if (name_str.rfind("aoc:u", 0) == 0) target_kind = KIND_AOC;

                open_kind = target_kind != KIND_NONE ? target_kind : KIND_SM;
                result_code = 0;
            }
            break;
        }

        case KIND_LM: {
            if (cmd_id == 0) {
                // OpenLogger -> returns KIND_LOGGER
                open_kind = KIND_LOGGER;
                result_code = 0;
            }
            break;
        }

        case KIND_LOGGER: {
            if (cmd_id == 0) {
                // Log -> accept log packet
                result_code = 0;
            }
            break;
        }

        case KIND_APM: {
            if (cmd_id == 0) {
                // OpenSession -> returns KIND_APM_SESSION
                open_kind = KIND_APM_SESSION;
                result_code = 0;
            } else if (cmd_id == 1) {
                static uint32_t val = 1;
                out_data = &val;
                out_data_len = sizeof(val);
                result_code = 0;
            }
            break;
        }

        case KIND_APM_SESSION: {
            if (cmd_id == 0) {
                // SetPerformanceConfiguration
                result_code = 0;
            } else if (cmd_id == 1) {
                // GetPerformanceConfiguration -> 0x92220008 (PerformanceConfiguration_DockedDefault)
                static uint32_t config = 0x92220008;
                out_data = &config;
                out_data_len = sizeof(config);
                result_code = 0;
            }
            break;
        }

        case KIND_APPLET: {
            if (cmd_id == 0) {
                // OpenApplicationProxy
                open_kind = KIND_APP_PROXY;
                result_code = 0;
            }
            break;
        }

        case KIND_APP_PROXY: {
            if (cmd_id == 0) {
                // GetCommonStateGetter
                open_kind = KIND_COMMON_STATE;
                result_code = 0;
            } else if (cmd_id == 1) {
                // GetSelfController
                open_kind = KIND_SELF_CONTROLLER;
                result_code = 0;
            } else if (cmd_id == 2) {
                // GetWindowController
                open_kind = KIND_WINDOW_CONTROLLER;
                result_code = 0;
            } else if (cmd_id == 3) {
                // GetAudioController
                open_kind = KIND_AUDIO_CONTROLLER;
                result_code = 0;
            } else if (cmd_id == 4) {
                // GetDisplayController
                open_kind = KIND_DISPLAY_CONTROLLER;
                result_code = 0;
            } else if (cmd_id == 20) {
                // GetApplicationFunctions
                open_kind = KIND_APPLICATION_FUNCTIONS;
                result_code = 0;
            }
            break;
        }

        case KIND_COMMON_STATE: {
            if (cmd_id == 0) {
                // GetEventObserver / GetEventHandle
                result_code = 0;
            } else if (cmd_id == 1) {
                // ReceiveMessage (drain queue then return AM_NO_MESSAGE 0x680)
                static int s_msg_count = 1;
                if (s_msg_count > 0) {
                    s_msg_count--;
                    static uint32_t msg = 0x0F; // FocusStateChanged (In focus)
                    out_data = &msg;
                    out_data_len = sizeof(msg);
                    result_code = 0;
                } else {
                    result_code = 0x680; // AM_NO_MESSAGE (module 128, desc 3)
                }
            } else if (cmd_id == 5) {
                // GetOperationMode: 1 (docked)
                static uint8_t op_mode = 1;
                out_data = &op_mode;
                out_data_len = sizeof(op_mode);
                result_code = 0;
            } else if (cmd_id == 6) {
                // GetPerformanceMode: 1 (boost)
                static uint32_t perf_mode = 1;
                out_data = &perf_mode;
                out_data_len = sizeof(perf_mode);
                result_code = 0;
            } else if (cmd_id == 8) {
                // GetBootMode: 0 (normal)
                static uint8_t boot_mode = 0;
                out_data = &boot_mode;
                out_data_len = sizeof(boot_mode);
                result_code = 0;
            } else if (cmd_id == 9) {
                // GetCurrentFocusState: 1 (in focus)
                static uint8_t focus = 1;
                out_data = &focus;
                out_data_len = sizeof(focus);
                result_code = 0;
            } else if (cmd_id == 60) {
                // GetDefaultDisplayResolution: 1920x1080
                static int32_t res[2] = {1920, 1080};
                out_data = res;
                out_data_len = sizeof(res);
                result_code = 0;
            }
            break;
        }

        case KIND_SELF_CONTROLLER: {
            if (cmd_id == 0) {
                // SetOperationModeChangedNotification
                result_code = 0;
            } else if (cmd_id == 1) {
                // SetPerformanceModeChangedNotification
                result_code = 0;
            } else if (cmd_id == 11) {
                // SetFocusHandlingMode
                result_code = 0;
            } else if (cmd_id == 12) {
                // SetRestartMessageIsEnabled
                result_code = 0;
            }
            break;
        }

        case KIND_PCTL: {
            if (cmd_id == 0 || cmd_id == 1) {
                open_kind = KIND_PCTL_SERVICE;
                result_code = 0;
            }
            break;
        }

        case KIND_PCTL_SERVICE: {
            static uint32_t zero = 0;
            out_data = &zero;
            out_data_len = sizeof(zero);
            result_code = 0;
            break;
        }

        case KIND_AOC: {
            static uint64_t zero = 0;
            out_data = &zero;
            out_data_len = sizeof(zero);
            result_code = 0;
            break;
        }

        case KIND_WINDOW_CONTROLLER: {
            if (cmd_id == 1) {
                // GetAppletResourceUserId -> 1
                static uint64_t aruid = 1;
                out_data = &aruid;
                out_data_len = sizeof(aruid);
                result_code = 0;
            }
            break;
        }

        case KIND_FS: {
            if (cmd_id == 1) {
                // SetCurrentProcess
                result_code = 0;
            } else if (cmd_id == 11 || cmd_id == 200) {
                // OpenRomFs / OpenDataStorageByCurrentProcess
                open_kind = KIND_ROM_STORAGE;
                result_code = 0;
            } else if (cmd_id == 18 || cmd_id == 51) {
                // OpenSaveDataFileSystem
                open_kind = KIND_SAVE_FS;
                result_code = 0;
            } else if (cmd_id == 1005) {
                // GetAccessLogMode
                static uint32_t log_mode = 0;
                out_data = &log_mode;
                out_data_len = sizeof(log_mode);
                result_code = 0;
            }
            break;
        }

        case KIND_ROM_STORAGE: {
            if (cmd_id == 4) {
                // GetSize
                static uint64_t romfs_sz = 0x80000000ULL; // 2 GiB
                out_data = &romfs_sz;
                out_data_len = sizeof(romfs_sz);
                result_code = 0;
            } else if (cmd_id == 0) {
                // Read
                result_code = 0;
            }
            break;
        }

        case KIND_TIME: {
            if (cmd_id == 0 || cmd_id == 1 || cmd_id == 4 || cmd_id == 5) {
                // GetStandardUserSystemClock
                open_kind = KIND_USER_CLOCK;
                result_code = 0;
            } else if (cmd_id == 2) {
                // GetStandardSteadyClock
                open_kind = KIND_STEADY_CLOCK;
                result_code = 0;
            } else if (cmd_id == 20) {
                // GetSharedMemoryNativeHandle
                new_handle = 0x30001;
                result_code = 0;
            }
            break;
        }

        case KIND_APPLICATION_FUNCTIONS: {
            if (cmd_id == 1) {
                // PopLaunchParameter(param_kind) -> LAUNCH_STORAGE
                static bool s_preselected_pending = true;
                uint32_t param_kind = (payload_len >= 4) ? *reinterpret_cast<const uint32_t *>(payload) : 0;
                if (param_kind == 2 && s_preselected_pending) {
                    s_preselected_pending = false;
                    open_kind = KIND_LAUNCH_STORAGE;
                    result_code = 0;
                } else {
                    result_code = 0x480; // AM_NO_PRESELECTED_USER
                }
            } else if (cmd_id == 20) {
                // EnsureSaveData
                result_code = 0;
            } else if (cmd_id == 21) {
                // GetDesiredLanguage -> zh-Hans
                static const char lang[8] = "zh-Hans";
                out_data = lang;
                out_data_len = sizeof(lang);
                result_code = 0;
            } else if (cmd_id == 22) {
                // TerminateApplication
                result_code = 0;
            } else if (cmd_id == 23) {
                // GetDisplayVersion -> 1.4.3
                static const char ver[16] = "1.4.3";
                out_data = ver;
                out_data_len = sizeof(ver);
                result_code = 0;
            } else if (cmd_id == 27) {
                // NotifyRunning
                static uint8_t ready = 1;
                out_data = &ready;
                out_data_len = sizeof(ready);
                result_code = 0;
            } else if (cmd_id == 40) {
                // NotifyBootFinished
                result_code = 0;
            } else if (cmd_id == 123) {
                // GetPreviousProgramIndex -> -1
                static int32_t prev_idx = -1;
                out_data = &prev_idx;
                out_data_len = sizeof(prev_idx);
                result_code = 0;
            }
            break;
        }

        case KIND_LAUNCH_STORAGE: {
            if (cmd_id == 0) {
                // OpenAccessor
                open_kind = KIND_LAUNCH_ACCESSOR;
                result_code = 0;
            }
            break;
        }

        case KIND_LAUNCH_ACCESSOR: {
            static const uint32_t s_preselected_user[0x88 / 4] = {
                0xc79497ca, 1, 1,
                1, 0, 0, 0
            };
            if (cmd_id == 0) {
                // GetSize -> 0x88 (136 bytes)
                static uint64_t sz = sizeof(s_preselected_user);
                out_data = &sz;
                out_data_len = sizeof(sz);
                result_code = 0;
            } else if (cmd_id == 11) {
                // Read(offset, out_buf, size)
                uint64_t offset = (payload_len >= 8) ? *reinterpret_cast<const uint64_t *>(payload) : 0;
                uint64_t read_len = sizeof(s_preselected_user);
                if (offset < sizeof(s_preselected_user)) {
                    read_len -= offset;
                } else {
                    read_len = 0;
                }

                // Check descriptors for receive buffer
                for (size_t d = 8; d + 12 <= aligned_cmif; d += 4) {
                    uint32_t w0 = *reinterpret_cast<uint32_t *>(bytes + d);
                    uint32_t w1 = *reinterpret_cast<uint32_t *>(bytes + d + 4);
                    uint32_t w2 = *reinterpret_cast<uint32_t *>(bytes + d + 8);
                    uint64_t blen = w0 | ((uint64_t)((w2 >> 24) & 15) << 32);
                    uint64_t baddr = w1 | ((uint64_t)(w2 >> 28) << 32) | ((uint64_t)((w2 >> 2) & 0x3fffff) << 36);
                    if (baddr >= 0x7000000000ULL && baddr < 0x7300000000ULL && blen > 0) {
                        size_t copy_cnt = std::min(static_cast<size_t>(blen), static_cast<size_t>(read_len));
                        std::memcpy(reinterpret_cast<void *>(baddr), reinterpret_cast<const uint8_t *>(s_preselected_user) + offset, copy_cnt);
                        break;
                    }
                }
                result_code = 0;
            }
            break;
        }

        case KIND_ACCOUNT: {
            if (cmd_id == 0) {
                // GetUserCount -> returns 1
                static uint32_t count = 1;
                out_data = &count;
                out_data_len = sizeof(count);
                result_code = 0;
            } else if (cmd_id == 1) {
                // GetUserExistence -> true
                static uint32_t exists = 1;
                out_data = &exists;
                out_data_len = sizeof(exists);
                result_code = 0;
            } else if (cmd_id == 2) {
                // ListAllUsers
                for (size_t d = 8; d + 12 <= aligned_cmif; d += 4) {
                    uint32_t w1 = *reinterpret_cast<uint32_t *>(bytes + d + 4);
                    uint32_t w2 = *reinterpret_cast<uint32_t *>(bytes + d + 8);
                    uint64_t baddr = w1 | ((uint64_t)(w2 >> 28) << 32) | ((uint64_t)((w2 >> 2) & 0x3fffff) << 36);
                    if (baddr >= 0x7000000000ULL && baddr < 0x7300000000ULL) {
                        uint64_t uid[2] = {1, 0};
                        std::memcpy(reinterpret_cast<void *>(baddr), uid, sizeof(uid));
                        break;
                    }
                }
                result_code = 0;
            } else if (cmd_id == 3 || cmd_id == 5) {
                // GetProfile / GetProfileEditor
                open_kind = KIND_ACCOUNT_PROFILE;
                result_code = 0;
            } else if (cmd_id == 100 || cmd_id == 140 || cmd_id == 160) {
                // InitializeApplicationInfo / InitializeApplicationInfoV0
                result_code = 0;
            } else if (cmd_id == 101) {
                // GetAccountManager
                open_kind = KIND_ACCOUNT_MANAGER;
                result_code = 0;
            } else if (cmd_id == 150) {
                // IsUserAccountSwitchLocked -> 0
                static uint8_t locked = 0;
                out_data = &locked;
                out_data_len = sizeof(locked);
                result_code = 0;
            }
            break;
        }

        case KIND_ACCOUNT_PROFILE: {
            if (cmd_id == 0) {
                result_code = 0;
            } else if (cmd_id == 1) {
                // GetBase
                static uint8_t profile[0x38]{0};
                uint64_t uid[2] = {1, 0};
                std::memcpy(profile, uid, sizeof(uid));
                std::memcpy(profile + 24, "Local Player", 13);
                out_data = profile;
                out_data_len = sizeof(profile);
                result_code = 0;
            }
            break;
        }

        case KIND_ACCOUNT_MANAGER: {
            result_code = 0;
            break;
        }

        case KIND_NVDRV: {
            if (cmd_id == 3) {
                // Initialize
                result_code = 0;
            } else if (cmd_id == 8) {
                // SetAruid
                result_code = 0;
            } else if (cmd_id == 13) {
                // GetStatus -> 0
                static uint32_t status = 0;
                out_data = &status;
                out_data_len = sizeof(status);
                result_code = 0;
            }
            break;
        }

        case KIND_AUD_MANAGER: {
            if (cmd_id == 0) {
                // OpenAudioRenderer
                open_kind = KIND_AUD_RENDERER;
                result_code = 0;
            } else if (cmd_id == 1) {
                // GetAudioRendererWorkBufferSize
                static uint64_t work_sz = 1024 * 1024; // 1 MiB work buffer
                out_data = &work_sz;
                out_data_len = sizeof(work_sz);
                result_code = 0;
            }
            break;
        }

        default:
            result_code = 0;
            break;
    }

    uint32_t new_object_id = 0;
    if (open_kind != KIND_NONE) {
        if (domain) {
            for (unsigned i = 0; i < 16; ++i) {
                if (session.domain_kinds[i] == KIND_NONE) {
                    session.domain_kinds[i] = open_kind;
                    session.domain_objects[i] = i + 1;
                    new_object_id = i + 1;
                    break;
                }
            }
        } else {
            new_handle = allocate_session(open_kind);
        }
    }

    trace_entry.result_code = result_code;
    m_trace_log.push_back(trace_entry);

    write_reply(bytes, size, tipc, token, new_handle, domain, new_object_id, out_data, out_data_len, result_code);
    return result_code;
}

} // namespace totk::runtime
