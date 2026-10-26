### Summary of Changes

This pull request resolves runtime thread scheduling warnings and cleans up compiler warnings on Linux.

#### 1. Realtime Thread Scheduling Policy (`src/gfx/compat/gfx_compat.h`)
- **Problem**: Calls to `thread_policy_set` in `src/gfx/nvn/hle_nvn.c` (`set_time_constraint_comp` and `set_time_constraint_comp_x`) logged `"present-thread time-constraint policy: failed"` because the Darwin Mach compat shim unconditionally returned `KERN_FAILURE`.
- **Fix**: Implemented `thread_policy_set` to dispatch to `plat_thread_realtime` (which invokes Linux `pthread_setschedparam(SCHED_FIFO)` and falls back to `setpriority(PRIO_PROCESS, ... -10)`). Returns `KERN_SUCCESS` so present and translation threads are correctly configured for low-jitter frame pacing.

#### 2. Compiler Warning Cleanup (`src/host/boot.c` & `src/host/threads.c`)
- Guarded `static char g_exefs[4096];` with `#ifdef _WIN32` in `src/host/boot.c` to eliminate `-Wunused-variable` warnings on Linux where configuration parsing differs.
- Cast `t->entry` and `t->stack_top` to `(unsigned long long)` in `src/host/threads.c`'s `LOG("CreateThread...")` macro to resolve format specifier mismatches on LP64 architectures.

### Verification
- Built with Clang 18 on Linux (`x86_64`) under `-Wall -Wextra` with zero warnings.
- Verified patch application with `--whitespace=error-all`.
