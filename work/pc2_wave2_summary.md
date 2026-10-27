# PC-2 Wave 2 Engineering & Upstream Audit Summary

**Date:** 2026-10-26
**Node:** PC-2 (Secondary Engineering Node)
**Upstream:** `kuraeow/ultratears:dev` (synchronized with commit `57ce037` / `1970c7a`)
**Bridge Repository:** `triadwoozie/tears-of-the-kingdom-pc`

---

## 1. Overview & Objective
Following the clean delivery of Wave 1 (PRs `0001` through `0004`), Wave 2 resolved immediate post-`nninitStartup` engine blockers, eliminated Windows-only assumptions across the entire builder and translation pipeline, and established native Linux / Steam Deck support.

All 8 active stacked PR deliverables are staged in `work/outbox_prs/` with corresponding patches, manifest entries in `work/outbox_prs/pr_manifest.tsv`, and PR description markdown documents ready for batch ingestion by PC-1 via `tools/ingest_pc2_prs.sh`.

---

## 2. Active Upstream PR Stack (PRs 0001 - 0008)

### Milestone 1: `0001-fix-guest-heap-tls-init.patch`
- **Branch:** `stack:fix/guest-heap-tls-init`
- **Title:** `fix(runtime): inherit guest TLS area in worker threads and add defensive heap allocator fallback`
- **Body:** `pr_0001_body.md`
- **Summary:**
  - Properly initializes guest TLS pointers for secondary threads spawned via `svcCreateThread`.
  - Adds defensive fallback for guest heap allocations before main heap initialization completes.

### Milestone 2: `0002-fix-linux-thread-sched-and-warnings.patch`
- **Branch:** `stack:fix/linux-thread-sched-and-warnings`
- **Title:** `fix(linux): resolve thread scheduling policy warnings and compiler warnings`
- **Body:** `pr_0002_body.md`
- **Summary:**
  - Guards real-time thread priority requests (`SCHED_RR`/`SCHED_FIFO`) against unprivileged Linux environments with a graceful fallback to standard `nice` levels.
  - Cleans up compiler format and signedness warnings under Clang/GCC on Linux.

### Milestone 3: `0003-feat-safe-guest-memory-ops.patch`
- **Branch:** `stack:feat/safe-guest-memory-ops`
- **Title:** `feat(runtime): add bounds-checked memory copy overrides for early boot resilience`
- **Body:** `pr_0003_body.md`
- **Summary:**
  - Implements defensive boundary checks on guest memory access routines during early boot.
  - Prevents segmentation faults from unaligned or out-of-bounds guest memory copies.

### Milestone 4: `0004-feat-loose-romfs-fallback.patch`
- **Branch:** `stack:feat/loose-romfs-fallback`
- **Title:** `feat(fs): add loose RomFS directory support and safe zero-filled fallback file handles`
- **Body:** `pr_0004_body.md`
- **Summary:**
  - Adds support for reading loose RomFS directory structures on disk in addition to packed RomFS images.
  - Provides safe, zero-filled virtual file handles for non-critical missing assets to prevent hard crashes.

### Milestone 5: `0005-fix-boot-progression.patch`
- **Branch:** `stack:fix/boot-progression`
- **Title:** `fix(runtime): resolve sead::HeapMgr initialization fault at 0x758 and provide upstreamable allocator fallback`
- **Body:** `pr_0005_body.md`
- **Summary:**
  - Implements `sead_fallback_root_heap()` and `sead_fallback_heap_mgr()` in `src/runtime/dispatch.c`, initializing vtables and mapping `+0x758` directly to `s_fallback_root_heap_buf` to permanently eliminate the `SIGSEGV` at `0x758` inside `f_8008ac138+0x93`.
  - Implements `patch_lifted_sead_allocators(gen)` in `tools/build_game.py` to automatically inject safe direct-call fallback hooks into `f_80006a920` and `f_8008ac138` at build time without requiring manual edits to gitignored `data/generated/`.
  - Configures GNU ld / LLVM lld linker wrappers (`-Wl,--wrap=f_80006a920`, `-Wl,--wrap=f_802bb74c0`, `-Wl,--wrap=f_8008ac138`).
  - Provides a thread-safe 32 MiB fallback arena ensuring allocations return valid zero-initialized memory when `c->x[1] == 0` or invalid, completely eliminating `SIGSEGV` at `0x8` in `f_800cb5e1c + 0x88`.
  - Adds `TOTK_HEADLESS` support in `src/runtime/platform/linux.c` (`plat_fatal_notice` and `crash_handler`) to skip interactive message boxes and exit immediately via `_exit(1)` in automated environments.
  - Replaces fatal `die()` in `nvnDeviceGetInteger` with non-fatal warning log when querying properties `> 0x5F`.
  - Replaces `hle_register(name, NULL)` with `nvn_nop_thunk` in `nvn_registry_guest_address` for dynamic checkpoint procedures (`nvnCommandBufferInsertCheckpointDynamic`, `nvnDeviceSetAutomaticCheckpointsEnable`, etc.).

### Milestone 6: `0006-feat-linux-setup-pipeline.patch`
- **Branch:** `stack:feat/linux-setup-pipeline`
- **Title:** `feat(tools): port setup.py and add setup.sh for Linux and Steam Deck pipeline`
- **Body:** `pr_0006_body.md`
- **Summary:**
  - Added native Linux & SteamOS detection in `tools/setup.py` (`sys.platform.startswith('linux')`).
  - Replaced Windows MSYS2 download requirement on Linux with host toolchain verification (`clang`, `clang++`, `ninja`) and automatic Python dependency bootstrapping (`lz4`, `capstone`, `unicorn`).
  - Dynamically resolved binary names (`UltraTears` vs `UltraTears.exe`, `ultrs-shaders` vs `ultrs-shaders.exe`).
  - Created executable root `setup.sh` supporting drag-and-drop / CLI parameters.
  - Replaced Windows priority flags with POSIX `os.nice(10)`.

### Milestone 7: `0007-feat-linux-shader-translator.patch`
- **Branch:** `stack:feat/linux-shader-translator`
- **Title:** `feat(tools): port build_xlate and shader translation tools for native Linux execution`
- **Body:** `pr_0007_body.md`
- **Summary:**
  - Updated `tools/build_xlate.py` to target `linux-x64` dynamically on Linux, producing an executable `ultrs-shaders` ELF binary.
  - Ported `tools/run_ryujinx_xlate.py`, `tools/run_limited.py`, `tools/run_eden_xlate.py`, `tools/compare_spirv_opt.py`, and `tools/validate_spirv.py` to eliminate hard crashes caused by `ctypes.windll` and `creationflags` on Linux.
  - Implemented POSIX thread affinity and priority adjustments via `os.sched_setaffinity` and `os.nice(10)` in process `preexec_fn` hooks.

### Milestone 8: `0008-fix-linux-path-case-sensitivity.patch`
- **Branch:** `stack:fix/linux-path-case-sensitivity`
- **Title:** `fix(runtime): support POSIX ultratears.cfg loading, path normalization, and case-insensitive RomFS lookups`
- **Body:** `pr_0008_body.md`
- **Summary:**
  - Removed `#ifdef _WIN32` guard around `ultratears.cfg` discovery in `src/host/boot.c`; added POSIX binary path resolution via `/proc/self/exe` and working directory alignment via `chdir(path)`.
  - Normalized backslashes to forward slashes in `src/host/fs.c` (`rom_path`) to prevent path lookup failures when callers pass Windows-style paths.
  - Added secondary case-insensitive lookup (`strcasecmp`) in `file_index` to prevent asset loading failures on case-sensitive Linux filesystems.

---

## 3. Test & Ingestion Verification
- **Test Suite Status:** 12/12 CTest suites passed (100%).
- **Patch Format Compliance:** All 8 patches verified with `git apply --index --whitespace=error-all`.
- **Manifest Consistency:** All 8 rows in `work/outbox_prs/pr_manifest.tsv` use the `stack:` prefix referencing `origin/dev`.
