#include "dispatch.h"
#include "runtime/kernel_hle.h"
#include "runtime/services_hle.h"
#include "runtime/memory_manager.h"
#include "runtime/threads_hle.h"
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <algorithm>
#include <chrono>

// 2-level Radix lookup table for guest addresses (4-byte aligned instructions)
// Level 1: bits [39:20] (up to 1M entries, allocated on demand)
// Level 2: bits [19:2]  (256K pointers)
static constexpr size_t L1_SIZE = 1048576;
static constexpr size_t L2_SIZE = 262144;
static RecompiledFunc **g_radix_l1 = nullptr;

// Fallback unordered_map for addresses outside standard module base
static std::unordered_map<uint64_t, RecompiledFunc> g_fallback_map;

// Symbol map loaded from work/symbols.tsv
static std::unordered_map<uint64_t, std::string> g_symbols;

void load_symbols_tsv(const char *path) {
    g_symbols.clear();
    FILE *f = std::fopen(path, "r");
    if (!f) return;
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char *tab1 = std::strchr(line, '\t');
        if (!tab1) continue;
        *tab1 = '\0';
        uint64_t addr = std::strtoull(line, nullptr, 16);
        char *tab2 = std::strchr(tab1 + 1, '\t');
        if (!tab2) continue;
        char *name = tab2 + 1;
        size_t len = std::strlen(name);
        while (len > 0 && (name[len - 1] == '\r' || name[len - 1] == '\n')) {
            name[--len] = '\0';
        }
        g_symbols[addr] = name;
    }
    std::fclose(f);
}

const char *lookup_symbol_name(uint64_t addr) {
    auto it = g_symbols.find(addr);
    if (it != g_symbols.end()) return it->second.c_str();
    return nullptr;
}

// Stack mapping tracker for Horizon OS svcMapMemory / svcQueryMemory
struct StackMapping {
    uint64_t src = 0;
    uint64_t dst = 0;
    uint64_t size = 0;
};
static StackMapping g_stack_mappings[64]{};

extern "C" {

void init_dispatch_table(void) {
    if (!g_radix_l1) {
        g_radix_l1 = static_cast<RecompiledFunc **>(std::calloc(L1_SIZE, sizeof(RecompiledFunc *)));
    }
}

void register_recompiled_func(uint64_t guest_addr, RecompiledFunc fn) {
    init_dispatch_table();

    uint64_t l1_idx = (guest_addr >> 20) & (L1_SIZE - 1);
    uint64_t l2_idx = (guest_addr >> 2) & (L2_SIZE - 1);

    if (!g_radix_l1[l1_idx]) {
        g_radix_l1[l1_idx] = static_cast<RecompiledFunc *>(std::calloc(L2_SIZE, sizeof(RecompiledFunc)));
    }

    g_radix_l1[l1_idx][l2_idx] = fn;
    g_fallback_map[guest_addr] = fn;
}

RecompiledFunc lookup_recompiled_func(uint64_t guest_addr) {
    if (!g_radix_l1) return nullptr;
    uint64_t l1_idx = (guest_addr >> 20) & (L1_SIZE - 1);
    uint64_t l2_idx = (guest_addr >> 2) & (L2_SIZE - 1);
    if (!g_radix_l1[l1_idx]) return nullptr;
    return g_radix_l1[l1_idx][l2_idx];
}

void dump_cpu_state(const Arm64Context *ctx, uint64_t target_addr) {
    if (!ctx) return;
    const char *target_sym = lookup_symbol_name(target_addr);
    const char *lr_sym = lookup_symbol_name(ctx->x[30]);
    std::fprintf(stderr, "\n==================== ARM64 CPU STATE CRASH DUMP ====================\n");
    std::fprintf(stderr, "  Target / PC : 0x%016llx%s%s%s   LR (X30) : 0x%016llx%s%s%s\n",
                 static_cast<unsigned long long>(target_addr),
                 target_sym ? " (" : "", target_sym ? target_sym : "", target_sym ? ")" : "",
                 static_cast<unsigned long long>(ctx->x[30]),
                 lr_sym ? " (" : "", lr_sym ? lr_sym : "", lr_sym ? ")" : "");
    std::fprintf(stderr, "  SP          : 0x%016llx   TPIDR_RO : 0x%016llx\n",
                 static_cast<unsigned long long>(ctx->sp),
                 static_cast<unsigned long long>(ctx->tpidrro));
    std::fprintf(stderr, "  NZCV        : 0x%08x (N=%u Z=%u C=%u V=%u)\n",
                 static_cast<uint32_t>(ctx->nzcv),
                 static_cast<uint32_t>((ctx->nzcv >> 31) & 1),
                 static_cast<uint32_t>((ctx->nzcv >> 30) & 1),
                 static_cast<uint32_t>((ctx->nzcv >> 29) & 1),
                 static_cast<uint32_t>((ctx->nzcv >> 28) & 1));
    std::fprintf(stderr, "  General Purpose Registers:\n");
    for (int i = 0; i < 32; i += 4) {
        std::fprintf(stderr, "    X%02d: 0x%016llx  X%02d: 0x%016llx  X%02d: 0x%016llx  X%02d: 0x%016llx\n",
                     i, static_cast<unsigned long long>(ctx->x[i]),
                     i + 1, static_cast<unsigned long long>(ctx->x[i + 1]),
                     i + 2, static_cast<unsigned long long>(ctx->x[i + 2]),
                     i + 3, static_cast<unsigned long long>(ctx->x[i + 3]));
    }
    std::fprintf(stderr, "====================================================================\n\n");
}

int g_trace_guest_calls = 0;

struct ActiveGuestThread {
    uint32_t handle{0};
    std::string name{"worker"};
    std::atomic<uint64_t> current_pc{0};
    std::atomic<uint64_t> current_lr{0};
    std::atomic<uint64_t> current_sp{0};
    std::atomic<uint64_t> current_fp{0};
    std::atomic<const char *> state{"Running"};

    // Horizon synchronization
    std::condition_variable cv;
    int wkind{0}; // 0=none, 1=lock, 2=condvar, 3=addr/futex, 4=sync
    int woke{0};
    uint64_t waddr{0};
    uint64_t wkey{0};
    uint32_t wtag{0};
    int64_t wres{0};
    uint64_t wseq{0};
    int32_t prio{44};
    int cancel{0};
    bool exited{false};
    ActiveGuestThread *wait_next{nullptr};
};

enum HleHandleType {
    HLE_HANDLE_FREE = 0,
    HLE_HANDLE_THREAD,
    HLE_HANDLE_EVENT_R,
    HLE_HANDLE_EVENT_W
};

struct HleEvent {
    bool signaled{false};
    int refs{2};
};

struct HleHandleEntry {
    HleHandleType type{HLE_HANDLE_FREE};
    void *obj{nullptr};
};

static constexpr uint32_t MAX_HANDLES = 4096;
static HleHandleEntry s_handle_table[MAX_HANDLES];

static uint32_t alloc_handle(HleHandleType type, void *obj) {
    for (uint32_t i = 1; i < MAX_HANDLES; i++) {
        if (s_handle_table[i].type == HLE_HANDLE_FREE) {
            s_handle_table[i].type = type;
            s_handle_table[i].obj = obj;
            return 0x1000 + i;
        }
    }
    return 0;
}

static HleEvent *get_event(uint32_t h, HleHandleType expected) {
    if (h < 0x1001 || h >= 0x1000 + MAX_HANDLES) return nullptr;
    auto &entry = s_handle_table[h - 0x1000];
    if (entry.type == expected || (expected == HLE_HANDLE_EVENT_R && (entry.type == HLE_HANDLE_EVENT_R || entry.type == HLE_HANDLE_EVENT_W))) {
        return reinterpret_cast<HleEvent *>(entry.obj);
    }
    return nullptr;
}

static void close_event_handle(uint32_t h) {
    if (h < 0x1001 || h >= 0x1000 + MAX_HANDLES) return;
    auto &entry = s_handle_table[h - 0x1000];
    if (entry.type == HLE_HANDLE_EVENT_R || entry.type == HLE_HANDLE_EVENT_W) {
        auto *e = reinterpret_cast<HleEvent *>(entry.obj);
        if (e && --e->refs <= 0) {
            delete e;
        }
        entry.type = HLE_HANDLE_FREE;
        entry.obj = nullptr;
    }
}

static std::mutex s_sync_mtx;
static uint64_t s_wseq = 0;
static ActiveGuestThread *s_wait_head = nullptr;

static constexpr uint32_t WAITERS_BIT = 0x40000000u;
static constexpr uint32_t R_TIMEOUT = 0xea01u;
static constexpr uint32_t R_CANCELLED = 0xec01u;
static constexpr uint32_t R_BAD_HANDLE = 0xe401u;
static constexpr uint32_t R_BAD_STATE = 0xfa01u;

static inline uint32_t ld32(uint64_t a) {
    return __atomic_load_n(reinterpret_cast<const volatile uint32_t *>(a), __ATOMIC_SEQ_CST);
}
static inline void st32(uint64_t a, uint32_t v) {
    __atomic_store_n(reinterpret_cast<volatile uint32_t *>(a), v, __ATOMIC_SEQ_CST);
}
static inline bool cas32(uint64_t a, uint32_t *exp, uint32_t v) {
    return __atomic_compare_exchange_n(reinterpret_cast<volatile uint32_t *>(a), exp, v, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static void wait_unlink(ActiveGuestThread *t) {
    for (ActiveGuestThread **p = &s_wait_head; *p; p = &(*p)->wait_next) {
        if (*p == t) {
            *p = t->wait_next;
            break;
        }
    }
    t->wait_next = nullptr;
}

static ActiveGuestThread *pick_waiter(int kind, uint64_t a) {
    ActiveGuestThread *best = nullptr;
    for (ActiveGuestThread *t = s_wait_head; t; t = t->wait_next) {
        if (t->wkind != kind || (kind == 2 ? t->wkey : t->waddr) != a) continue;
        if (!best || t->prio < best->prio || (t->prio == best->prio && t->wseq < best->wseq)) {
            best = t;
        }
    }
    return best;
}

static void wake_thread(ActiveGuestThread *t, int64_t res) {
    wait_unlink(t);
    t->wkind = 0;
    t->woke = 1;
    t->wres = res;
    t->cv.notify_one();
}

static int64_t block_thread(ActiveGuestThread *t, int kind, int64_t timeout_ns, std::unique_lock<std::mutex> &lock) {
    t->wkind = kind;
    t->woke = 0;
    t->wait_next = s_wait_head;
    s_wait_head = t;
    t->wseq = ++s_wseq;

    if (timeout_ns == 0) {
        wait_unlink(t);
        t->wkind = 0;
        return R_TIMEOUT;
    }

    if (timeout_ns > 0) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(timeout_ns);
        while (!t->woke) {
            if (t->cv.wait_until(lock, deadline) == std::cv_status::timeout && !t->woke) {
                wait_unlink(t);
                t->wkind = 0;
                return R_TIMEOUT;
            }
        }
    } else {
        while (!t->woke) {
            t->cv.wait(lock);
        }
    }
    return t->wres;
}

static void lock_handoff(uint64_t addr) {
    ActiveGuestThread *w = pick_waiter(1, addr);
    if (!w) {
        st32(addr, 0);
        return;
    }
    int more = 0;
    for (ActiveGuestThread *t = s_wait_head; t; t = t->wait_next) {
        if (t != w && t->wkind == 1 && t->waddr == addr) {
            more = 1;
            break;
        }
    }
    st32(addr, w->wtag | (more ? WAITERS_BIT : 0));
    wake_thread(w, 0);
}

static void lock_acquire_for(ActiveGuestThread *t, uint64_t addr, uint32_t tag) {
    uint32_t v = ld32(addr);
    for (;;) {
        if (v == 0) {
            if (cas32(addr, &v, tag)) {
                wake_thread(t, 0);
                return;
            }
            continue;
        }
        if (cas32(addr, &v, v | WAITERS_BIT)) break;
    }
    t->wkind = 1;
    t->waddr = addr;
    t->wtag = tag;
    t->wseq = ++s_wseq;
}

static std::mutex s_threads_mutex;
static std::vector<ActiveGuestThread *> s_active_threads;
static thread_local ActiveGuestThread *t_current_guest_thread = nullptr;
static thread_local Arm64Context *t_active_guest_ctx = nullptr;
extern "C" Arm64Context *get_active_guest_context(void) {
    return t_active_guest_ctx;
}
static std::atomic<uint64_t> s_total_indirect_calls{0};
static std::atomic<bool> s_watchdog_running{false};

static ActiveGuestThread *cur_guest_thread() {
    if (t_current_guest_thread) return t_current_guest_thread;
    auto *main_thr = new ActiveGuestThread();
    main_thr->handle = 1;
    main_thr->name = "MainThread";
    main_thr->prio = 44;
    t_current_guest_thread = main_thr;
    {
        std::lock_guard<std::mutex> lock(s_threads_mutex);
        s_active_threads.push_back(main_thr);
    }
    return main_thr;
}

static ActiveGuestThread *get_thread(uint32_t h) {
    std::lock_guard<std::mutex> lock(s_threads_mutex);
    for (auto *t : s_active_threads) {
        if (t->handle == h) return t;
    }
    return nullptr;
}

struct HleGuestThreadDef {
    uint32_t handle;
    uint64_t entry;
    uint64_t arg;
    uint64_t stack_top;
    int32_t prio;
    int32_t core;
    uint64_t tlr_addr;
    uint64_t tls_addr;
};

static std::unordered_map<uint32_t, HleGuestThreadDef> s_hle_threads_def;
static std::atomic<uint32_t> s_next_thread_handle{0x20000};

void guest_indirect_call(Arm64Context *ctx, uint64_t target_addr) {
    s_total_indirect_calls++;

    if (!t_current_guest_thread) {
        auto *main_thr = new ActiveGuestThread();
        main_thr->handle = 1;
        main_thr->name = "MainThread";
        main_thr->current_pc = target_addr;
        main_thr->current_lr = ctx->x[30];
        main_thr->current_sp = ctx->sp;
        t_current_guest_thread = main_thr;
        {
            std::lock_guard<std::mutex> lock(s_threads_mutex);
            s_active_threads.push_back(main_thr);
        }

        if (!s_watchdog_running.exchange(true)) {
            std::thread watchdog([]() {
                uint64_t last_calls = 0;
                while (s_watchdog_running) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    uint64_t cur = s_total_indirect_calls.load();
                    uint64_t rate = cur - last_calls;
                    last_calls = cur;
                    std::printf("[Watchdog 1s] Total Calls: %llu (+%llu calls/s) | Active Guest Threads: %zu\n",
                                static_cast<unsigned long long>(cur),
                                static_cast<unsigned long long>(rate),
                                s_active_threads.size());
                    std::lock_guard<std::mutex> lock(s_threads_mutex);
                    for (auto *t : s_active_threads) {
                        uint64_t pc = t->current_pc.load();
                        uint64_t lr = t->current_lr.load();
                        const char *sym_pc = lookup_symbol_name(pc);
                        const char *sym_lr = lookup_symbol_name(lr);
                        std::printf("  - Thread 0x%x (%s) [%s]: PC=0x%llx%s%s%s LR=0x%llx%s%s%s SP=0x%llx\n",
                                    t->handle, t->name.c_str(), t->state.load(),
                                    static_cast<unsigned long long>(pc),
                                    sym_pc ? " (" : "", sym_pc ? sym_pc : "", sym_pc ? ")" : "",
                                    static_cast<unsigned long long>(lr),
                                    sym_lr ? " (" : "", sym_lr ? sym_lr : "", sym_lr ? ")" : "",
                                    static_cast<unsigned long long>(t->current_sp.load()));
                        if (t->handle == 1) {
                            uint64_t cur_fp = t->current_fp.load();
                            for (int f = 0; f < 10 && cur_fp >= 0x7000000000ULL && cur_fp < totk::runtime::GUEST_MEMORY_LIMIT && (cur_fp & 7) == 0; ++f) {
                                uint64_t caller_lr = *reinterpret_cast<uint64_t *>(cur_fp + 8);
                                uint64_t prev_fp = *reinterpret_cast<uint64_t *>(cur_fp);
                                const char *sym = lookup_symbol_name(caller_lr);
                                std::printf("       #%d: 0x%llx%s%s%s (FP=0x%llx)\n",
                                            f, static_cast<unsigned long long>(caller_lr),
                                            sym ? " (" : "", sym ? sym : "", sym ? ")" : "",
                                            static_cast<unsigned long long>(cur_fp));
                                if (prev_fp <= cur_fp) break;
                                cur_fp = prev_fp;
                            }
                        }
                    }
                    std::fflush(stdout);
                }
            });
            watchdog.detach();
        }
    } else {
        t_current_guest_thread->current_pc = target_addr;
        t_current_guest_thread->current_lr = ctx->x[30];
        t_current_guest_thread->current_sp = ctx->sp;
        t_current_guest_thread->current_fp = ctx->x[29];
    }
    t_active_guest_ctx = ctx;

    // Fast check: Registered HLE/Fallback functions (NVN thunks, sead overrides, etc.)
    auto it_fb = g_fallback_map.find(target_addr);
    if (it_fb != g_fallback_map.end() && it_fb->second) {
        it_fb->second(ctx);
        return;
    }

    // VTable / Indirect-Call Guard:
    // If target_addr is outside guest .text range (0x7100000000 .. 0x7110000000) or null or unaligned:
    if (target_addr < 0x7100000000ULL || target_addr >= 0x7110000000ULL || (target_addr & 3) != 0) {
        static std::unordered_set<uint64_t> s_warned_lrs;
        static std::mutex s_warn_mtx;
        bool first_time = false;
        {
            std::lock_guard<std::mutex> lock(s_warn_mtx);
            first_time = s_warned_lrs.insert(ctx->x[30]).second;
        }
        if (first_time) {
            std::printf("  [guest_indirect_call] Ignored invalid indirect target 0x%llx from LR=0x%llx (X0=0x%llx) -> returning 0\n",
                        static_cast<unsigned long long>(target_addr),
                        static_cast<unsigned long long>(ctx->x[30]),
                        static_cast<unsigned long long>(ctx->x[0]));
            std::fflush(stdout);
        }
        ctx->x[0] = 0;
        ctx->pc = ctx->x[30];
        return;
    }

    if (g_trace_guest_calls > 0) {
        const char *sym = lookup_symbol_name(target_addr);
        std::printf("  [TRACE #%d] Target=0x%llx%s%s%s LR=0x%llx SP=0x%llx X0=0x%llx\n",
                    g_trace_guest_calls--,
                    static_cast<unsigned long long>(target_addr),
                    sym ? " (" : "", sym ? sym : "", sym ? ")" : "",
                    static_cast<unsigned long long>(ctx->x[30]),
                    static_cast<unsigned long long>(ctx->sp),
                    static_cast<unsigned long long>(ctx->x[0]));
        std::fflush(stdout);
    }

    if (g_radix_l1) {
        uint64_t l1_idx = (target_addr >> 20) & (L1_SIZE - 1);
        uint64_t l2_idx = (target_addr >> 2) & (L2_SIZE - 1);
        if (g_radix_l1[l1_idx] && g_radix_l1[l1_idx][l2_idx]) {
            g_radix_l1[l1_idx][l2_idx](ctx);
            return;
        }
    }


    const char *target_sym = lookup_symbol_name(target_addr);
    const char *lr_sym = lookup_symbol_name(ctx->x[30]);
    std::fprintf(stderr, "[Dispatch Error] Unresolved indirect guest call to 0x%016llx%s%s%s (LR=0x%016llx%s%s%s)\n",
                 static_cast<unsigned long long>(target_addr),
                 target_sym ? " (" : "", target_sym ? target_sym : "", target_sym ? ")" : "",
                 static_cast<unsigned long long>(ctx->x[30]),
                 lr_sym ? " (" : "", lr_sym ? lr_sym : "", lr_sym ? ")" : "");
    dump_cpu_state(ctx, target_addr);
}

extern "C" void rt_dispatch(Arm64Context *c) {
    bool in_guest_range = (c->pc >= 0x7100000000ULL && c->pc < 0x7110000000ULL) ||
                          (c->pc >= 0x7180000000ULL && c->pc < 0x7190000000ULL);
    if ((!in_guest_range || ((c->pc & 3ULL) != 0)) && g_fallback_map.find(c->pc) == g_fallback_map.end()) {
        static std::unordered_set<uint64_t> s_warned_pc;
        static std::mutex s_warn_pc_mtx;
        bool first = false;
        {
            std::lock_guard<std::mutex> lock(s_warn_pc_mtx);
            first = s_warned_pc.insert(c->x[30]).second;
        }
        if (first) {
            std::printf("  [rt_dispatch] Ignored invalid indirect target 0x%llx from LR=0x%llx\n",
                        static_cast<unsigned long long>(c->pc),
                        static_cast<unsigned long long>(c->x[30]));
            std::fflush(stdout);
        }
        c->x[0] = 0;
        c->pc = c->x[30];
        return;
    }
    guest_indirect_call(c, c->pc);
}

extern "C" uint64_t resolve_v143_jump_table(uint64_t anchor_addr, int32_t index) {
    // Exact table offsets verified in totk-mobile/tools/patch_module.py
    switch (anchor_addr) {
        case 0x393ba0: {
            static const int32_t offsets[6] = {0x14, 0x20, 0x2c, 0x38, 0x44, 0x50};
            if (index >= 0 && index < 6) return anchor_addr + offsets[index];
            break;
        }
        case 0x596d20: {
            static const int32_t offsets[9] = {0x14, 0x24, 0x34, 0x44, 0x54, 0x64, 0x74, 0x84, 0x94};
            if (index >= 0 && index < 9) return anchor_addr + offsets[index];
            break;
        }
        case 0x598ee4: {
            static const int32_t offsets[9] = {0x10, 0x1c, 0x28, 0x34, 0x40, 0x4c, 0x58, 0x64, 0x70};
            if (index >= 0 && index < 9) return anchor_addr + offsets[index];
            break;
        }
        case 0x5f2a90: {
            static const int32_t offsets[5] = {0x14, 0x20, 0x2c, 0x38, 0x44};
            if (index >= 0 && index < 5) return anchor_addr + offsets[index];
            break;
        }
        case 0x6397cc: {
            static const int32_t offsets[7] = {0x14, 0x24, 0x34, 0x44, 0x54, 0x64, 0x74};
            if (index >= 0 && index < 7) return anchor_addr + offsets[index];
            break;
        }
        default:
            break;
    }
    return 0;
}

extern "C" void dispatch_blr_x18(Arm64Context *ctx) {
    // AAPCS64: x16/x17 are scratch registers, x18 is platform register.
    // In TotK v1.4.3, blr x18 routes through Horizon IPC / HLE system calls.
    // LR (x30) contains the return address in the guest module.
    (void)ctx;
}

void rt_udf(Arm64Context *c, uint64_t pc) {
    std::fprintf(stderr, "[Trap] Undefined instruction / trap at 0x%016llx\n",
                 static_cast<unsigned long long>(pc));
    dump_cpu_state(c, pc);
}

void rt_brk(Arm64Context *c, uint32_t imm, uint64_t pc) {
    std::fprintf(stderr, "[Trap] BRK #%u at 0x%016llx\n", imm,
                 static_cast<unsigned long long>(pc));
    dump_cpu_state(c, pc);
    std::abort();
}

uint64_t rt_ticks(void) {
    // 19.2 MHz Horizon system clock
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 19200000ULL + static_cast<uint64_t>(ts.tv_nsec) * 192ULL / 10000ULL;
}

int rt_hook(Arm64Context *c, uint64_t site) {
    (void)c;
    (void)site;
    return 0; // run original instruction
}

static uint64_t s_heap_size = 0;

void rt_svc(Arm64Context *c, uint32_t imm, uint64_t pc) {
    auto &k = totk::runtime::KernelHLE::instance();
    switch (imm) {
        case 0x01: { // svcSetHeapSize
            uint64_t size = c->x[1];
            constexpr uint64_t heap_base = 0x7200000000ULL;
            s_heap_size = size;
            if (size > 0) {
                totk::runtime::MemoryManager::instance().commit(heap_base, size);
            }
            c->x[0] = totk::runtime::ResultSuccess;
            c->x[1] = heap_base;
            break;
        }
        case 0x02: { // svcSetMemoryPermission(addr, size, perm)
            c->x[0] = totk::runtime::ResultSuccess;
            break;
        }
        case 0x03: { // svcSetMemoryAttribute(addr, size, mask, attr)
            c->x[0] = totk::runtime::ResultSuccess;
            break;
        }
        case 0x04: { // svcMapMemory(dst, src, size)
            uint64_t dst = c->x[0];
            uint64_t src = c->x[1];
            uint64_t size = c->x[2];
            for (auto &m : g_stack_mappings) {
                if (m.size == 0) {
                    m = {src, dst, size};
                    break;
                }
            }
            if (dst >= 0x7000000000ULL && dst < totk::runtime::GUEST_MEMORY_LIMIT && size > 0) {
                totk::runtime::MemoryManager::instance().commit(dst, size);
            }
            c->x[0] = totk::runtime::ResultSuccess;
            break;
        }
        case 0x05: { // svcUnmapMemory(dst, src, size)
            uint64_t dst = c->x[0];
            for (auto &m : g_stack_mappings) {
                if (m.dst == dst) {
                    m = {};
                    break;
                }
            }
            c->x[0] = totk::runtime::ResultSuccess;
            break;
        }
        case 0x06: { // svcQueryMemory(out_info, page_info, addr)
            struct MemoryInfo {
                uint64_t base;
                uint64_t size;
                uint32_t state;
                uint32_t attribute;
                uint32_t permission;
                uint32_t ipc_count;
                uint32_t device_count;
                uint32_t padding;
            };
            auto *info = reinterpret_cast<MemoryInfo *>(c->x[0]);
            uint64_t addr = c->x[2];
            if (info) {
                std::memset(info, 0, sizeof(MemoryInfo));
                bool mapped_stack = false;
                for (const auto &m : g_stack_mappings) {
                    if (m.size > 0) {
                        if (addr >= m.dst && addr < m.dst + m.size) {
                            info->base = m.dst;
                            info->size = m.size;
                            info->state = 11; // MemState_Stack
                            info->permission = 3;
                            info->attribute = 0;
                            mapped_stack = true;
                            break;
                        }
                        if (addr >= m.src && addr < m.src + m.size) {
                            info->base = m.src;
                            info->size = m.size;
                            info->state = 5; // MemState_Heap
                            info->permission = 3;
                            info->attribute = 1; // MemAttr_Borrowed
                            mapped_stack = true;
                            break;
                        }
                    }
                }
                if (!mapped_stack) {
                    if (addr < 0x7000000000ULL) {
                        info->base = 0;
                        info->size = 0x7000000000ULL;
                        info->state = 0; // MemState_Free
                        info->permission = 0;
                    } else if (addr >= 0x7080000000ULL && addr < 0x7080400000ULL) {
                        info->base = 0x7080000000ULL;
                        info->size = 0x400000ULL;
                        info->state = 11; // MemState_Stack
                        info->permission = 3; // Read/Write
                    } else if (addr >= 0x7080400000ULL && addr < 0x7080410000ULL) {
                        info->base = 0x7080400000ULL;
                        info->size = 0x10000ULL;
                        info->state = 12; // MemState_ThreadLocal
                        info->permission = 3;
                    } else if (addr >= 0x7100000000ULL && addr < 0x7110000000ULL) {
                        info->base = 0x7100000000ULL;
                        info->size = 0x10000000ULL;
                        info->state = 3; // MemState_Code
                        info->permission = 5; // Read/Execute
                    } else if (addr >= 0x7200000000ULL && addr < 0x7200000000ULL + (s_heap_size ? s_heap_size : 0x80000000ULL)) {
                        info->base = 0x7200000000ULL;
                        info->size = (s_heap_size ? s_heap_size : 0x80000000ULL);
                        info->state = 5; // MemState_Heap
                        info->permission = 3;
                    } else {
                        info->base = addr & ~0xFFFULL;
                        info->size = 0x10000ULL;
                        info->state = 0; // Free
                        info->permission = 0;
                    }
                }
            }
            c->x[0] = totk::runtime::ResultSuccess;
            c->x[1] = 0;
            break;
        }
        case 0x08: { // svcCreateThread(out_handle, entry, arg, stack_top, prio, cpu_id)
            uint32_t handle = s_next_thread_handle++;
            HleGuestThreadDef t{};
            t.handle = handle;
            t.entry = c->x[1];
            t.arg = c->x[2];
            t.stack_top = c->x[3] & ~0xFull;
            t.prio = static_cast<int32_t>(c->x[4]);
            t.core = static_cast<int32_t>(c->x[5]);

            uint64_t tlr_addr = 0x7080500000ULL + (handle - 0x20000) * 0x1000ULL;
            totk::runtime::MemoryManager::instance().commit(tlr_addr, 0x1000ULL);
            uint64_t tls_addr = tlr_addr + 0x200ULL;
            t.tlr_addr = tlr_addr;
            t.tls_addr = tls_addr;

            *reinterpret_cast<uint64_t *>(tlr_addr + 0x1f0) = tls_addr;
            *reinterpret_cast<uint64_t *>(tlr_addr + 0x1f8) = t.arg;
            if (t.arg >= 0x7000000000ULL && t.arg < totk::runtime::GUEST_MEMORY_LIMIT) {
                *reinterpret_cast<uint32_t *>(t.arg + 0x1b0) = handle;
            }

            if (t.stack_top >= 0x7000000000ULL && t.stack_top < totk::runtime::GUEST_MEMORY_LIMIT) {
                uint64_t stack_base = (t.stack_top > 0x100000ULL) ? (t.stack_top - 0x100000ULL) : 0x7000000000ULL;
                totk::runtime::MemoryManager::instance().commit(stack_base, 0x100000ULL);
            }

            {
                std::lock_guard<std::mutex> lock(s_threads_mutex);
                s_hle_threads_def[handle] = t;
            }

            std::printf("  [Kernel HLE] svcCreateThread handle=0x%x entry=0x%llx arg=0x%llx stack=0x%llx prio=%d core=%d\n",
                        handle, static_cast<unsigned long long>(t.entry),
                        static_cast<unsigned long long>(t.arg),
                        static_cast<unsigned long long>(t.stack_top),
                        t.prio, t.core);
            std::fflush(stdout);

            c->x[0] = totk::runtime::ResultSuccess;
            c->x[1] = handle;
            break;
        }
        case 0x09: { // svcStartThread(handle)
            uint32_t handle = static_cast<uint32_t>(c->x[0]);
            HleGuestThreadDef t{};
            bool found = false;
            {
                std::lock_guard<std::mutex> lock(s_threads_mutex);
                auto it = s_hle_threads_def.find(handle);
                if (it != s_hle_threads_def.end()) {
                    t = it->second;
                    found = true;
                }
            }
            if (found) {
                auto *wthr = new ActiveGuestThread();
                wthr->handle = t.handle;
                wthr->prio = t.prio;
                char buf[32];
                std::snprintf(buf, sizeof(buf), "Worker_0x%x", t.handle);
                wthr->name = buf;
                wthr->current_pc = t.entry;
                wthr->current_lr = 0;
                wthr->current_sp = t.stack_top;
                wthr->state = "Starting";
                {
                    std::lock_guard<std::mutex> lock(s_threads_mutex);
                    s_active_threads.push_back(wthr);
                }

                std::printf("  [Kernel HLE] svcStartThread handle=0x%x -> spawning host thread!\n", handle);
                std::fflush(stdout);

                std::thread([t, wthr]() {
                    t_current_guest_thread = wthr;
                    totk::runtime::t_guest_tls = reinterpret_cast<uint8_t *>(t.tlr_addr);
                    alignas(16) Arm64Context thread_ctx{};
                    thread_ctx.sp = t.stack_top;
                    thread_ctx.x[0] = t.arg;
                    thread_ctx.x[30] = 0;
                    thread_ctx.pc = t.entry;
                    thread_ctx.tpidr = t.tls_addr;
                    thread_ctx.tpidrro = t.tlr_addr;
                    wthr->state = "Running";

                    guest_indirect_call(&thread_ctx, t.entry);

                    wthr->exited = true;
                    wthr->state = "Finished";
                    {
                        std::unique_lock<std::mutex> lock(s_sync_mtx);
                        for (ActiveGuestThread *w = s_wait_head; w; ) {
                            ActiveGuestThread *next = w->wait_next;
                            if (w->wkind == 4) wake_thread(w, 0);
                            w = next;
                        }
                    }
                    std::printf("  [Kernel HLE] Worker thread 0x%x finished execution\n", t.handle);
                    std::fflush(stdout);
                }).detach();
                c->x[0] = totk::runtime::ResultSuccess;
            } else {
                c->x[0] = totk::runtime::ResultInvalidHandle;
            }
            break;
        }
        case 0x0a: { // svcExitThread()
            if (t_current_guest_thread) {
                t_current_guest_thread->exited = true;
                t_current_guest_thread->state = "Exited";
                {
                    std::unique_lock<std::mutex> lock(s_sync_mtx);
                    for (ActiveGuestThread *w = s_wait_head; w; ) {
                        ActiveGuestThread *next = w->wait_next;
                        if (w->wkind == 4) wake_thread(w, 0);
                        w = next;
                    }
                }
            }
            c->x[0] = totk::runtime::ResultSuccess;
            break;
        }
        case 0x0b: { // svcSleepThread(ns)
            if (t_current_guest_thread) t_current_guest_thread->state = "Sleeping";
            c->x[0] = k.svc_sleep_thread(static_cast<int64_t>(c->x[0]));
            if (t_current_guest_thread) t_current_guest_thread->state = "Running";
            break;
        }
        case 0x0c: { // svcGetThreadPriority(out_prio, handle)
            c->x[0] = totk::runtime::ResultSuccess;
            c->x[1] = 44; // Default main thread priority in Horizon
            break;
        }
        case 0x0d: { // svcSetThreadPriority
            c->x[0] = totk::runtime::ResultSuccess;
            break;
        }
        case 0x0e: { // svcGetThreadCoreMask(out_ideal_core, out_affinity_mask, handle)
            c->x[0] = totk::runtime::ResultSuccess;
            c->x[1] = 0; // ideal core 0
            break;
        }
        case 0x0f: { // svcSetThreadCoreMask(handle, ideal_core, affinity_mask)
            c->x[0] = totk::runtime::ResultSuccess;
            break;
        }
        case 0x10: { // svcGetCurrentProcessorNumber
            c->x[0] = 0; // core 0
            break;
        }
        case 0x11: { // svcSignalEvent(handle)
            uint32_t h = static_cast<uint32_t>(c->x[0]);
            std::unique_lock<std::mutex> lock(s_sync_mtx);
            auto *e = get_event(h, HLE_HANDLE_EVENT_W);
            if (!e) e = get_event(h, HLE_HANDLE_EVENT_R);
            if (e) {
                e->signaled = true;
                for (ActiveGuestThread *w = s_wait_head; w; ) {
                    ActiveGuestThread *next = w->wait_next;
                    if (w->wkind == 4) wake_thread(w, 0);
                    w = next;
                }
                c->x[0] = totk::runtime::ResultSuccess;
            } else {
                c->x[0] = R_BAD_HANDLE;
            }
            break;
        }
        case 0x12: { // svcClearEvent(handle)
            uint32_t h = static_cast<uint32_t>(c->x[0]);
            std::unique_lock<std::mutex> lock(s_sync_mtx);
            auto *e = get_event(h, HLE_HANDLE_EVENT_W);
            if (!e) e = get_event(h, HLE_HANDLE_EVENT_R);
            if (e) {
                e->signaled = false;
                c->x[0] = totk::runtime::ResultSuccess;
            } else {
                c->x[0] = R_BAD_HANDLE;
            }
            break;
        }
        case 0x13: { // svcMapSharedMemory(handle, addr, size, perm)
            uint32_t handle = static_cast<uint32_t>(c->x[0]);
            uint64_t addr = c->x[1];
            uint64_t size = c->x[2];
            if (addr >= 0x7000000000ULL && addr < totk::runtime::GUEST_MEMORY_LIMIT && size > 0) {
                totk::runtime::MemoryManager::instance().commit(addr, size);
                if (handle == 0x30001 || (addr & 0xFFF) == 0) {
                    uint8_t *m = reinterpret_cast<uint8_t *>(addr);
                    static const uint8_t clock_uuid[16] = {0x4c, 0x41, 0x52, 0x65, 0x63, 0x6f, 0x6d, 0x70, 1, 2, 3, 4, 5, 6, 7, 8};
                    int64_t steady = static_cast<int64_t>(rt_ticks() / 19200000ULL);
                    int64_t unix_now = static_cast<int64_t>(time(nullptr));

                    // 0x00: SteadyClockContext
                    *reinterpret_cast<uint32_t *>(m) = 0;
                    *reinterpret_cast<int64_t *>(m + 0x08) = 0;
                    std::memcpy(m + 0x10, clock_uuid, 16);

                    // 0x38: Local system clock context
                    *reinterpret_cast<uint32_t *>(m + 0x38) = 0;
                    *reinterpret_cast<int64_t *>(m + 0x40) = unix_now - steady;
                    *reinterpret_cast<int64_t *>(m + 0x48) = steady;
                    std::memcpy(m + 0x50, clock_uuid, 16);

                    // 0x80: Network system clock context
                    *reinterpret_cast<uint32_t *>(m + 0x80) = 0;
                    *reinterpret_cast<int64_t *>(m + 0x88) = unix_now - steady;
                    *reinterpret_cast<int64_t *>(m + 0x90) = steady;
                    std::memcpy(m + 0x98, clock_uuid, 16);

                    // 0xC8: Automatic correction
                    *reinterpret_cast<uint32_t *>(m + 0xC8) = 0;
                    m[0xCC] = 0;
                }
            }
            c->x[0] = totk::runtime::ResultSuccess;
            break;
        }
        case 0x14: { // svcUnmapSharedMemory(handle, addr, size)
            c->x[0] = totk::runtime::ResultSuccess;
            break;
        }
        case 0x15: { // svcCreateTransferMemory(out_handle, addr, size, perm)
            static std::atomic<uint32_t> s_tmem_counter{0x30000};
            uint32_t thandle = s_tmem_counter++;
            uint64_t out_slot = c->x[0];
            if (out_slot >= 0x7000000000ULL && out_slot < totk::runtime::GUEST_MEMORY_LIMIT) {
                *reinterpret_cast<uint32_t *>(out_slot) = thandle;
            }
            c->x[0] = totk::runtime::ResultSuccess;
            c->x[1] = thandle;
            break;
        }
        case 0x16: { // svcCloseHandle
            uint32_t h = static_cast<uint32_t>(c->x[0]);
            {
                std::unique_lock<std::mutex> lock(s_sync_mtx);
                close_event_handle(h);
            }
            c->x[0] = k.svc_close_handle(h);
            break;
        }
        case 0x17: { // svcResetSignal
            uint32_t h = static_cast<uint32_t>(c->x[0]);
            std::unique_lock<std::mutex> lock(s_sync_mtx);
            auto *e = get_event(h, HLE_HANDLE_EVENT_R);
            if (!e) {
                c->x[0] = R_BAD_HANDLE;
            } else if (!e->signaled) {
                c->x[0] = R_BAD_STATE;
            } else {
                e->signaled = false;
                c->x[0] = totk::runtime::ResultSuccess;
            }
            break;
        }
        case 0x18: { // svcWaitSynchronization(out_index, handles_ptr, count, timeout_ns)
            uint64_t hs_ptr = c->x[1];
            int n = static_cast<int>(c->x[2]);
            int64_t ns = static_cast<int64_t>(c->x[3]);
            auto *t = cur_guest_thread();
            std::unique_lock<std::mutex> lock(s_sync_mtx);
            int64_t r = 0;
            int idx = -1;
            auto deadline = (ns > 0) ? (std::chrono::steady_clock::now() + std::chrono::nanoseconds(ns)) : std::chrono::steady_clock::time_point{};

            for (;;) {
                if (t->cancel) {
                    t->cancel = 0;
                    r = R_CANCELLED;
                    break;
                }
                bool bad = false;
                const uint32_t *hs = reinterpret_cast<const uint32_t *>(hs_ptr);
                for (int i = 0; i < n && idx < 0; i++) {
                    auto *e = get_event(hs[i], HLE_HANDLE_EVENT_R);
                    if (e && e->signaled) {
                        idx = i;
                    } else if (!e) {
                        auto *th = get_thread(hs[i]);
                        if (th && th->exited) {
                            idx = i;
                        } else if (!th) {
                            bad = true;
                        }
                    }
                }
                if (bad) { r = R_BAD_HANDLE; break; }
                if (idx >= 0) { r = 0; break; }
                if (ns == 0) { r = R_TIMEOUT; break; }
                int64_t left = (ns < 0) ? -1 : std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - std::chrono::steady_clock::now()).count();
                if (ns > 0 && left <= 0) { r = R_TIMEOUT; break; }
                if (block_thread(t, 4, left, lock) == R_TIMEOUT) { r = R_TIMEOUT; break; }
            }
            c->x[0] = static_cast<uint64_t>(r);
            c->x[1] = (idx >= 0) ? static_cast<uint64_t>(idx) : 0;
            break;
        }
        case 0x19: { // svcCancelSynchronization
            uint32_t th_handle = static_cast<uint32_t>(c->x[0]);
            std::unique_lock<std::mutex> lock(s_sync_mtx);
            auto *th = get_thread(th_handle);
            if (th) {
                th->cancel = 1;
                if (th->wkind == 4) wake_thread(th, -1);
                c->x[0] = totk::runtime::ResultSuccess;
            } else {
                c->x[0] = R_BAD_HANDLE;
            }
            break;
        }
        case 0x1a: { // svcArbitrateLock(owner_handle, addr, tag)
            uint32_t owner = static_cast<uint32_t>(c->x[0]);
            uint64_t addr = c->x[1];
            uint32_t tag = static_cast<uint32_t>(c->x[2]);
            auto *t = cur_guest_thread();
            std::unique_lock<std::mutex> lock(s_sync_mtx);
            int64_t r = 0;
            if (addr >= 0x7000000000ULL && addr < totk::runtime::GUEST_MEMORY_LIMIT) {
                if (ld32(addr) == (owner | WAITERS_BIT)) {
                    t->waddr = addr;
                    t->wtag = tag;
                    r = block_thread(t, 1, -1, lock);
                }
            }
            c->x[0] = static_cast<uint64_t>(r);
            break;
        }
        case 0x1b: { // svcArbitrateUnlock(addr)
            uint64_t addr = c->x[0];
            std::unique_lock<std::mutex> lock(s_sync_mtx);
            if (addr >= 0x7000000000ULL && addr < totk::runtime::GUEST_MEMORY_LIMIT) {
                lock_handoff(addr);
            }
            c->x[0] = totk::runtime::ResultSuccess;
            break;
        }
        case 0x1c: { // svcWaitProcessWideKeyAtomic(mutex_addr, cond_key, tag, timeout_ns)
            uint64_t addr = c->x[0];
            uint64_t key = c->x[1];
            uint32_t tag = static_cast<uint32_t>(c->x[2]);
            int64_t timeout_ns = static_cast<int64_t>(c->x[3]);
            auto *t = cur_guest_thread();
            std::unique_lock<std::mutex> lock(s_sync_mtx);
            st32(key, 1);
            t->wkey = key;
            t->waddr = addr;
            t->wtag = tag;
            lock_handoff(addr);
            int64_t r = block_thread(t, 2, timeout_ns, lock);
            c->x[0] = static_cast<uint64_t>(r);
            break;
        }
        case 0x1d: { // svcSignalProcessWideKey(cond_key, count)
            uint64_t key = c->x[0];
            int count = static_cast<int>(c->x[1]);
            std::unique_lock<std::mutex> lock(s_sync_mtx);
            int n = 0;
            for (ActiveGuestThread *w; (count <= 0 || n < count) && (w = pick_waiter(2, key)); n++) {
                lock_acquire_for(w, w->waddr, w->wtag);
            }
            int left = 0;
            for (ActiveGuestThread *w = s_wait_head; w; w = w->wait_next) {
                if (w->wkind == 2 && w->wkey == key) {
                    left = 1;
                    break;
                }
            }
            if (!left) st32(key, 0);
            c->x[0] = totk::runtime::ResultSuccess;
            break;
        }
        case 0x1e: { // svcGetSystemTick
            c->x[0] = rt_ticks();
            break;
        }
        case 0x25: { // svcGetThreadId(out_thread_id, handle)
            uint32_t handle = static_cast<uint32_t>(c->x[1]);
            c->x[0] = totk::runtime::ResultSuccess;
            c->x[1] = (handle != 0) ? handle : (t_current_guest_thread ? t_current_guest_thread->handle : 1);
            break;
        }
        case 0x1f: { // svcConnectToNamedPort(out_handle, name)
            uint32_t handle = 0;
            const char *name = reinterpret_cast<const char *>(c->x[1]);
            c->x[0] = k.svc_connect_to_named_port(&handle, name);
            c->x[1] = handle;
            break;
        }
        case 0x21: { // svcSendSyncRequest(session_handle)
            void *ipc_buf = reinterpret_cast<void *>(c->tpidrro);
            uint32_t handle = static_cast<uint32_t>(c->x[0]);
            c->x[0] = totk::runtime::ServicesHLE::instance().dispatch_ipc(handle, ipc_buf, 0x100);
            break;
        }
        case 0x22: { // svcSendSyncRequestWithUserBuffer(user_buffer, size, session_handle)
            void *ipc_buf = reinterpret_cast<void *>(c->x[0]);
            size_t size = static_cast<size_t>(c->x[1]);
            uint32_t handle = static_cast<uint32_t>(c->x[2]);
            c->x[0] = totk::runtime::ServicesHLE::instance().dispatch_ipc(handle, ipc_buf, size ? size : 0x100);
            break;
        }
        case 0x26: { // svcBreak(reason, info, size)
            uint32_t code = (c->x[1] && c->x[2] >= 4) ? *reinterpret_cast<uint32_t *>(c->x[1]) : 0;
            uint64_t fp = c->x[29];
            uint64_t lr = c->x[30];
            std::fprintf(stderr, "\n=======================================================\n");
            std::fprintf(stderr, "  [Kernel SVC svcBreak] reason=0x%llx info=0x%llx (Result=0x%x) size=0x%llx\n",
                         static_cast<unsigned long long>(c->x[0]),
                         static_cast<unsigned long long>(c->x[1]),
                         code,
                         static_cast<unsigned long long>(c->x[2]));
            std::fprintf(stderr, "  [svcBreak Stack Trace]\n");
            std::fprintf(stderr, "    #00 PC=0x%016llx LR=0x%016llx FP=0x%016llx\n",
                         static_cast<unsigned long long>(c->pc),
                         static_cast<unsigned long long>(lr),
                         static_cast<unsigned long long>(fp));
            for (int depth = 1; depth < 16 && fp > 0x7000000000ULL && fp < totk::runtime::GUEST_MEMORY_LIMIT; ++depth) {
                uint64_t prev_fp = *reinterpret_cast<uint64_t *>(fp);
                uint64_t caller_lr = *reinterpret_cast<uint64_t *>(fp + 8);
                std::fprintf(stderr, "    #%02d LR=0x%016llx (FP=0x%016llx)\n", depth,
                             static_cast<unsigned long long>(caller_lr),
                             static_cast<unsigned long long>(prev_fp));
                if (prev_fp <= fp || prev_fp >= totk::runtime::GUEST_MEMORY_LIMIT) break;
                fp = prev_fp;
            }
            std::fprintf(stderr, "=======================================================\n\n");
            // If in an abort sequence, break out rather than spinning in an infinite loop
            c->x[0] = totk::runtime::ResultSuccess;
            std::exit(1);
            break;
        }
        case 0x27: { // svcArbitrateLock (legacy)
            c->x[0] = k.svc_arbitrate_lock(static_cast<uint32_t>(c->x[0]), c->x[1], static_cast<uint32_t>(c->x[2]));
            break;
        }
        case 0x28: { // svcArbitrateUnlock (legacy)
            c->x[0] = k.svc_arbitrate_unlock(c->x[0]);
            break;
        }
        case 0x29: { // svcGetInfo
            uint32_t info_type = static_cast<uint32_t>(c->x[1]);
            uint64_t val = 0;
            switch (info_type) {
                case 0: val = 7; break;                          // Allowed virtual cores 0..2
                case 1: val = (1ULL << 60) - (1ULL << 28); break; // AllowedThreadPrioMask
                case 2: val = 0x7000000000ULL; break;            // MapPhysicalMemoryRange (ALIAS_OFFSET)
                case 3: val = 0x100000000ULL; break;             // ALIAS_SIZE (4 GiB)
                case 4: val = 0x7200000000ULL; break;            // HEAP_OFFSET
                case 5: val = 0x80000000ULL; break;              // HEAP_CAPACITY (2 GiB)
                case 6: val = 0x100000000ULL; break;             // MEMORY_LIMIT (4 GiB)
                case 7: val = 0x20000000ULL; break;              // used_memory + SYSTEM_RESERVE
                case 11: val = 0; break;                         // entropy
                case 12: val = 0x7000000000ULL; break;           // ASLR space base
                case 13: val = 0x100000000ULL; break;            // ASLR space size
                case 14: val = 0x7080000000ULL; break;           // STACK_OFFSET
                case 15: val = 0x400000ULL; break;               // STACK_SIZE (4 MiB)
                case 16: val = 0; break;                         // SYSTEM_RESERVE (0 disables VAMM)
                case 18: val = 0x0100F2C0115B6000ULL; break;     // TotK Title ID
                case 20: val = 0x7000000000ULL; break;           // ALIAS base
                case 21: val = 0x100000000ULL; break;            // ALIAS size
                case 22: val = 0x10000000ULL; break;             // used_memory
                case 28: val = 0; break;
                default: val = 0; break;
            }
            c->x[0] = totk::runtime::ResultSuccess;
            c->x[1] = val; // Horizon OS returns scalar values in X1!
            break;
        }
        case 0x2c: { // svcSetProcessMemoryPermission
            uint64_t addr = c->x[0];
            uint64_t size = c->x[1];
            if (addr >= 0x7000000000ULL && addr < totk::runtime::GUEST_MEMORY_LIMIT && size > 0) {
                totk::runtime::MemoryManager::instance().commit(addr, size);
            }
            c->x[0] = totk::runtime::ResultSuccess;
            break;
        }
        case 0x2d: { // svcMapProcessMemory
            c->x[0] = totk::runtime::ResultSuccess;
            break;
        }
        case 0x34: { // svcWaitForAddress(addr, type, val, timeout_ns)
            uint64_t addr = c->x[0];
            int type = static_cast<int>(c->x[1]);
            int32_t val = static_cast<int32_t>(c->x[2]);
            int64_t timeout_ns = static_cast<int64_t>(c->x[3]);
            if (addr < 0x7000000000ULL || addr >= totk::runtime::GUEST_MEMORY_LIMIT) {
                c->x[0] = R_BAD_STATE;
                break;
            }
            auto *t = cur_guest_thread();
            static std::atomic<int> s_log_count_34{0};
            if (s_log_count_34.fetch_add(1) < 40) {
                std::printf("  [Kernel HLE svcWaitForAddress] th=0x%x (%s) addr=0x%llx type=%d val=%d timeout=%lld\n",
                            t->handle, t->name.c_str(), static_cast<unsigned long long>(addr), type, val, static_cast<long long>(timeout_ns));
                std::fflush(stdout);
            }
            std::unique_lock<std::mutex> lock(s_sync_mtx);
            uint32_t v = ld32(addr);
            bool ok = (type == 2) ? (static_cast<int32_t>(v) == val) : (static_cast<int32_t>(v) < val);
            if (ok && type == 1) {
                while (!cas32(addr, &v, v - 1)) {
                    if (!((static_cast<int32_t>(v) < val))) { ok = false; break; }
                }
            }
            if (!ok) {
                c->x[0] = R_BAD_STATE;
            } else {
                t->waddr = addr;
                constexpr int64_t main_thread_watchdog_ns = 30'000'000'000LL;
                const int64_t effective_timeout_ns =
                    (timeout_ns == -1 && t->handle == 1) ? main_thread_watchdog_ns : timeout_ns;
                c->x[0] = static_cast<uint64_t>(block_thread(t, 3, effective_timeout_ns, lock));
            }
            break;
        }
        case 0x35: { // svcSignalToAddress(addr, type, val, count)
            uint64_t addr = c->x[0];
            int type = static_cast<int>(c->x[1]);
            int32_t val = static_cast<int32_t>(c->x[2]);
            int count = static_cast<int>(c->x[3]);
            if (addr < 0x7000000000ULL || addr >= totk::runtime::GUEST_MEMORY_LIMIT) {
                c->x[0] = R_BAD_STATE;
                break;
            }
            auto *t = cur_guest_thread();
            static std::atomic<int> s_log_count_35{0};
            if (s_log_count_35.fetch_add(1) < 40) {
                std::printf("  [Kernel HLE svcSignalToAddress] th=0x%x (%s) addr=0x%llx type=%d val=%d count=%d\n",
                            t->handle, t->name.c_str(), static_cast<unsigned long long>(addr), type, val, count);
                std::fflush(stdout);
            }
            std::unique_lock<std::mutex> lock(s_sync_mtx);
            int nw = 0;
            for (ActiveGuestThread *w = s_wait_head; w; w = w->wait_next) {
                if (w->wkind == 3 && w->waddr == addr) nw++;
            }
            int64_t r = 0;
            if (type != 0) {
                int32_t nv = val + 1;
                if (type == 2) {
                    if (count <= 0) nv = (nw > 0) ? (val - 1) : (val + 1);
                    else nv = (nw == 0) ? (val + 1) : (nw <= count ? (val - 1) : val);
                }
                uint32_t e = static_cast<uint32_t>(val);
                if (!cas32(addr, &e, static_cast<uint32_t>(nv))) r = R_BAD_STATE;
            }
            if (!r) {
                int n = 0;
                for (ActiveGuestThread *w; (count <= 0 || n < count) && (w = pick_waiter(3, addr)); n++) {
                    wake_thread(w, 0);
                }
            }
            c->x[0] = static_cast<uint64_t>(r);
            break;
        }
        case 0x45: { // svcCreateEvent(out_write_handle, out_read_handle)
            std::unique_lock<std::mutex> lock(s_sync_mtx);
            auto *e = new HleEvent();
            uint32_t hw = alloc_handle(HLE_HANDLE_EVENT_W, e);
            uint32_t hr = alloc_handle(HLE_HANDLE_EVENT_R, e);
            c->x[0] = totk::runtime::ResultSuccess;
            c->x[1] = hw;
            c->x[2] = hr;
            break;
        }
        default:
            std::printf("  [Kernel SVC #0x%02x] PC=0x%llx LR=0x%llx X0=0x%llx X1=0x%llx\n",
                        imm, static_cast<unsigned long long>(pc),
                        static_cast<unsigned long long>(c->x[30]),
                        static_cast<unsigned long long>(c->x[0]),
                        static_cast<unsigned long long>(c->x[1]));
            c->x[0] = 0;
            break;
    }
}

} // extern "C"
