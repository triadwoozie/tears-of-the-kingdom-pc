# PC-2 Wave 2 Engineering & Upstream Audit Summary

**Date:** 2026-10-26
**Node:** PC-2 (Secondary Engineering Node)
**Upstream:** `kuraeow/ultratears:dev` (synchronized with commit `57ce037` / `1970c7a`)
**Bridge Repository:** `triadwoozie/tears-of-the-kingdom-pc`

---

## 1. Overview & Objective
Following the clean delivery of Wave 1 (PRs `0001` through `0004`), Wave 2 focused on eliminating Windows-only assumptions across the entire builder, translation, and runtime pipeline, adding native Linux / Steam Deck support, establishing real-time telemetry HUD tooling, and unblocking subsequent guest initialization in `agl` / `gsys`.

All 9 PR deliverables are staged in `work/outbox_prs/` with corresponding patches, manifest entries in `work/outbox_prs/pr_manifest.tsv`, and PR description markdown documents ready for batch ingestion by PC-1 via `tools/ingest_pc2_prs.sh`.

---

## 2. Wave 2 Upstream PR Deliverables

### Milestone 5: `0005-feat-linux-setup-pipeline.patch`
- **Branch:** `stack:feat/linux-setup-pipeline`
- **Title:** `feat(tools): port setup.py and add setup.sh for Linux and Steam Deck pipeline`
- **Body:** `pr_0005_body.md`
- **Summary:**
  - Added native Linux & SteamOS detection in `tools/setup.py` (`sys.platform.startswith('linux')`).
  - Replaced Windows MSYS2 download requirement on Linux with host toolchain verification (`clang`, `clang++`, `ninja`) and automatic Python dependency bootstrapping (`lz4`, `capstone`, `unicorn`).
  - Dynamically resolved binary names (`UltraTears` vs `UltraTears.exe`, `ultrs-shaders` vs `ultrs-shaders.exe`).
  - Created executable root `setup.sh` supporting drag-and-drop / CLI parameters.
  - Replaced Windows priority flags with POSIX `os.nice(10)`.

### Milestone 6: `0006-feat-linux-shader-translator.patch`
- **Branch:** `stack:feat/linux-shader-translator`
- **Title:** `feat(tools): port build_xlate and shader translation tools for native Linux execution`
- **Body:** `pr_0006_body.md`
- **Summary:**
  - Updated `tools/build_xlate.py` to target `linux-x64` dynamically on Linux, producing an executable `ultrs-shaders` ELF binary.
  - Ported `tools/run_ryujinx_xlate.py`, `tools/run_limited.py`, `tools/run_eden_xlate.py`, `tools/compare_spirv_opt.py`, and `tools/validate_spirv.py` to eliminate hard crashes caused by `ctypes.windll` and `creationflags` on Linux.
  - Implemented POSIX thread affinity and priority adjustments via `os.sched_setaffinity` and `os.nice(10)` in process `preexec_fn` hooks.

### Milestone 7: `0007-fix-runtime-path-and-logic-audit.patch`
- **Branch:** `stack:fix/runtime-path-and-logic-audit`
- **Title:** `fix(runtime): support POSIX ultratears.cfg loading, path normalization, and case-insensitive RomFS lookups`
- **Body:** `pr_0007_body.md`
- **Summary:**
  - Removed `#ifdef _WIN32` guard around `ultratears.cfg` discovery in `src/host/boot.c`; added POSIX binary path resolution via `/proc/self/exe` and working directory alignment via `chdir(path)`.
  - Normalized backslashes to forward slashes in `src/host/fs.c` (`rom_path`) to prevent path lookup failures when callers pass Windows-style paths.
  - Added secondary case-insensitive lookup (`strcasecmp`) in `file_index` to prevent asset loading failures on case-sensitive Linux filesystems.

### Milestone 8: `0008-feat-telemetry-hud-and-cli-ui.patch`
- **Branch:** `stack:feat/telemetry-hud-and-cli-ui`
- **Title:** `feat(ui): add window title telemetry HUD, F3 toggle, and enhanced setup.py CLI progress UI`
- **Body:** `pr_0008_body.md`
- **Summary:**
  - Implemented real-time performance and system telemetry in the SDL3 window title bar: FPS, frame time (ms), active Vulkan GPU name (`vkc.props.deviceName`), allocated guest heap usage (`runtime_heap_bytes()`), and execution state.
  - Added interactive HUD toggle bound to `F3` and supported `TOTK_HUD=1` environment configuration.
  - Exported `runtime_heap_bytes()` in `src/host/svc.c`.
  - Redesigned `tools/setup.py` console output with structured ANSI boxes, step timers, and actionable failure troubleshooting diagnostics.

### Milestone 9: `0009-fix-boot-progression-next.patch`
- **Branch:** `stack:fix/boot-progression-next`
- **Title:** `fix(nvn): guard device integer queries and safely stub dynamic checkpoint proc addresses`
- **Body:** `pr_0009_body.md`
- **Summary:**
  - Guarded `nvnDeviceGetInteger` in `src/gfx/nvn/hle_nvn.c`: replaced fatal `die()` on unlisted property queries (`> 0x5F`) with a non-fatal warning log and zero return, allowing `agl` capability queries to succeed.
  - In `src/gfx/nvn/nvn_registry.c`: replaced `hle_register(name, NULL)` with a safe no-op thunk (`nvn_nop_thunk`) for unrecognized dynamic procedure addresses (such as `nvnCommandBufferInsertCheckpointDynamic`, `nvnDeviceSetAutomaticCheckpointsEnable`, and `nvnCommandBufferSetProvokingVertexMode`), preventing fatal `unimplemented host function` aborts during render pipeline bring-up.

---

## 3. Test & Ingestion Verification
- **Test Suite Status:** 12/12 CTest suites passed (100%).
- **Patch Format Compliance:** All 9 patches verified with `git apply --index --whitespace=error-all`.
- **Manifest Consistency:** All 9 rows in `work/outbox_prs/pr_manifest.tsv` use the `stack:` prefix referencing `origin/dev`.
