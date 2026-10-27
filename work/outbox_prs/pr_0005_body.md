### Summary of Changes

This pull request resolves the SEAD startup barriers in early `nnMain`:
1. Direct-call heap allocation bypass (`SIGSEGV` at `0x8` in `f_800cb5e1c+0x88` / `f_80006a920`).
2. `sead::HeapMgr` uninitialized singleton fault (`SIGSEGV` at `0x758` in `f_8008ac138+0x93`).
3. Automated build-time injection in `tools/build_game.py` so untracked `data/generated/` lifted C chunks are patched reproducibly without manual edits.
4. Headless test harness support (`TOTK_HEADLESS=1`) to prevent hangs during automated CI runs.
5. NVN capability query safety and dynamic checkpoint procedure address stubs.

---

#### 1. SEAD `HeapMgr` Initialization Fault at `0x758` (`f_8008ac138`)
- **Problem**: Following heap allocation in `f_800cb5c84`, execution invokes `f_8008ac138` at `+0x11c` (`sead::HeapMgr::findContainHeap`). Because `HeapMgr::sInstance` has not yet been initialized, `c->x[0]` is NULL (`0`). At instruction `+0x93`, the function dereferences `[c->x[0] + 0x758]`, immediately faulting with `SIGSEGV` at address `0x0000000000000758`.
- **Fix**:
  - Implemented `sead_fallback_root_heap()` and `sead_fallback_heap_mgr()` in `src/runtime/dispatch.c`:
    * Initializes valid vtables pointing to safe allocator thunks.
    * Explicitly maps offset `+0x758` on `s_fallback_heap_mgr_buf` directly to `s_fallback_root_heap_buf`.
  - Added `__wrap_f_8008ac138`: If `c->x[0]` is NULL or outside valid mapped guest address space, returns a valid backing pointer to `sead_fallback_root_heap()` immediately, unblocking caller `f_800cb5c84`.

#### 2. Upstreamable Direct Allocator Fallback (`tools/build_game.py`)
- **Problem**: In lifted C code, functions call `f_80006a920` and `f_8008ac138` via direct compiler calls within the recompiled `main` module, bypassing the indirect HLE dispatch table. Because `data/generated/` is ignored by Git, manual edits cannot be tracked upstream.
- **Fix**:
  - Added `patch_lifted_sead_allocators(gen)` to `tools/build_game.py`. During every build, it automatically inspects lifted `.c` files in `data/generated/main/` and injects safe fallback checks into `f_80006a920` and `f_8008ac138` before compilation.
  - Added `-Wl,--wrap=f_80006a920 -Wl,--wrap=f_802bb74c0 -Wl,--wrap=f_8008ac138` to the Linux linker command line.
  - Implemented `sead_fallback_heap_alloc` backed by a thread-safe 32 MiB arena in `src/runtime/dispatch.c`.

#### 3. Headless Crash Handler Flag (`src/runtime/platform/linux.c`)
- **Problem**: In automated testing and CI environments, fatal signals invoke `plat_fatal_notice()`, which creates interactive SDL error message dialogs (`SDL_ShowSimpleMessageBox`) that block automated execution.
- **Fix**: Added support for the `TOTK_HEADLESS` environment variable in `plat_fatal_notice()` and `crash_handler`. When set, the interactive message box is skipped and the process terminates immediately via `_exit(1)` after printing the backtrace.

#### 4. NVN Capability Integer Query Safety (`src/gfx/nvn/hle_nvn.c`)
- **Problem**: In `nvnDeviceGetInteger`, `agl` queries capability constants beyond the known SMO table range (e.g. `0x60`, `0x61`). The previous implementation called `die("unsupported NVN device property %#x", w)`, terminating the process during engine feature negotiation.
- **Fix**: Replaced the fatal abort with a diagnostic log and returned `*out = 0`, allowing the engine to gracefully fall back to standard rendering features.

#### 5. Safe Checkpoint & Debug Procedure Stubs (`src/gfx/nvn/nvn_registry.c`)
- **Problem**: Dynamic procedure address queries (`nvnDeviceGetProcAddress`) for checkpoint, z-cull compression, and debug markers (such as `nvnCommandBufferInsertCheckpointDynamic`, `nvnCommandBufferSetZCullZF32CompressionEnable`, `nvnDeviceSetAutomaticCheckpointsEnable`, `nvnDeviceSetCheckpointCallStacksEnable`) previously returned stubs registered with `NULL` callbacks. When called during command recording, `dispatch.c` terminated execution with `FATAL("unimplemented host function")`.
- **Fix**: Returned a safe, zero-overhead no-op thunk (`nvn_nop_thunk`) for unrecognized dynamic procedure lookups, allowing command buffer recording to proceed into presentation.

---

### Verification
- `git apply --check --whitespace=error-all work/outbox_prs/*.patch` passes cleanly across all 8 PRs.
- `src/runtime/dispatch.c` and `src/runtime/platform/linux.c` compile cleanly with zero errors under Clang.
- `tools/build_game.py` validates with Python bytecode compilation.
