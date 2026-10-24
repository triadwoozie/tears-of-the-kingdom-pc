# docs/06-STUB-AUDIT.md — Comprehensive Subsystem & Stub Coverage Audit

## 1. Executive Summary
This document provides a line-by-line audit comparing the current implementation of **`TotK-Recomp`** (`tools/`, `src/runtime/`, `src/graphics/`, `src/recompiler/`) against the upstream reference implementations in **`reference_repos/totk-mobile`** and **`reference_repos/LASRecomp`**.

While the Stage 0–6 architectural skeleton compiles with zero errors and passes 100% of unit tests, executing the real *The Legend of Zelda: Tears of the Kingdom* (v1.4.3) binary (~60 MiB of AArch64 code across `rtld`, `main`, `subsdk0`, and `sdk`) requires transitioning all synthetic test stubs into full production implementations.

---

## 2. Line-by-Line Subsystem Audit Table

| File / Component | Current Lines | Reference Equivalent | Reference Lines | Gap & Stub Status | Action Plan for Phase 7 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `tools/lift_nso.py` | 98 | `LASRecomp/tools/lifter.py` + `gen.py` | 1,248 | Currently generates synthetic functions `range(0, 0x10000, 0x20)`. Lifter core is in `tools/lifter.py` but not wired for end-to-end NSO lifting. | Wire `tools/lifter.py` and `simd_c.py` into `lift_nso.py`. Add `--lift-stats <nso>` CLI for 100% ISA disassembly and mnemonic coverage verification. |
| `tools/ingest_rom.py` | 193 | `totk-mobile/tools/extract_exefs.py` + `prepare_modules.py` | 285 | Verifies hashes and writes `provenance.toml`, but does not decompress LZ4 NSO segments into memory offsets. | Integrate `tools/nso.py` segment parser and LZ4 decompressor to validate `.text`, `.rodata`, `.data`, and `.bss` segments against official v1.4.3 build IDs. |
| `tools/prepare_shaders_vk.py` | 87 | `totk-mobile/script/translate_shaders.py` + `dmsh_xlate` | 375 | Packs placeholder SPIR-V data into `work/shaders/totk_vk_shaders.bin`. | Connect to `reference_repos/totk-mobile/core/gfx/tools/dmsh_xlate/` and `Ryujinx.Graphics.Shader` via `dotnet` to compile real `.spv` blobs directly from Maxwell shader archives. |
| `tools/generate_nvn_hle.py` | 88 | `totk-mobile/script/prepare_graphics.py` + `nvn_protos.h` | 1,585 | Generates only 5 basic dispatch stubs for `nvnDeviceGetProcAddress`. | Generate all 523 NVN API prototypes and entrypoints with LP64 AArch64 ABI argument extraction (`X0`–`X7`, `V0`–`V7`, stack args). |
| `src/runtime/module_loader.cpp` | 151 | `totk-mobile/host/probe.c` + `LASRecomp/tools/nso.py` | 387 | Relies solely on dynamic library loading (`dlopen`/`LoadLibraryA`); lacks native NSO0 binary loader and dynamic ELF relocations. | Implement native `NSO0` segment loader, 39-bit memory window mapping (`0x7100000000`), and `.rela.dyn` / `.dynsym` resolution (`R_AARCH64_RELATIVE`, `R_AARCH64_GLOB_DAT`, `R_AARCH64_JUMP_SLOT`, `R_AARCH64_ABS64`, `DT_RELR`). |
| `src/runtime/kernel_hle.cpp` | 105 | `totk-mobile/host/kernel.c` | 725 | Basic futex arbitration lock/unlock. Lacks full SVC call dispatch, transfer memory descriptors, and guest thread lifecycle management. | Port complete Horizon SVC table (`svcConnectToNamedPort`, `svcSendSyncRequest`, `svcMapSharedMemory`, `svcTransferMemory`, `svcSleepThread`, `svcGetThreadId`). |
| `src/runtime/services_hle.cpp` | 41 | `totk-mobile/host/services.c` | 1,051 | Stub returning success code 0 for any IPC call. | Port CMIF / Hipc parser, domain objects, handle descriptors, and handlers for `sm:`, `fsp-srv`, `set`, `time:u`, `acc:u0`, `apm`, `appletOE`, `audren:u`, `pl:u`, `vi:u`, `nvdrv`. |
| `src/runtime/filesystem_hle.cpp` | 49 | `totk-mobile/host/filesystem.c` | 356 | Simple directory check. Lacks multi-volume mounting and Zstandard `.zs` archive reading. | Port volume mounts (`work/save`, `work/temporary`, `work/cache`), RomFS directory traversal, and `.zs` decompressed streaming. |
| `src/graphics/hle_nvn_gen.cpp` | 210 | `totk-mobile/core/gfx/nvn/hle_nvn.c` | 1,364 | Handful of static NVN function pointers for device/queue/command buffer. | Implement full ~500 entrypoint table routing through Vulkan 1.3 backend. |
| `src/graphics/nvn_vk.cpp` | 421 | `totk-mobile/core/gfx/nvn/nvn_mtl.m` | 1,200 | Vulkan 1.3 setup (device, swapchain, render pass). Lacks full command buffer draw/dispatch/state recording. | Connect NVN command recording directly to Vulkan command buffers (`vkCmdDraw*`, `vkCmdDispatch`, `vkCmdBindVertexBuffers`, dynamic state). |
| `src/main.cpp` | 258 | `totk-mobile/host/probe.c` | 289 | Contains `--smoke-test` and SDL3 UI loop. Lacks guest boot sequence orchestration. | Add `--boot-trace` executing guest initialization sequence: `_init_libc0` -> `nnosInitialize` -> `_init_libc1` -> `nninitStartup` -> `_init_libc2` -> `nnMain` -> first frame presentation. |

---

## 3. Detailed Subsystem Audit & Gaps

### A. AArch64 ISA Lifter & Capstone Coverage
- **Current State:**
  - `tools/lifter.py` (728 lines) implements full instruction decoding (`dp_imm`, `branch_sys`, `ldst`, `dp_reg`, `fp_simd`).
  - `tools/simd_c.py` (1,156 lines) implements SIMDe AdvSIMD vector operations.
  - However, `tools/lift_nso.py` only synthesized dummy functions for unit tests instead of feeding instructions through `Lifter`.
- **Target Gate:**
  - Add `--lift-stats <nso>` in `tools/lift_nso.py` to disassemble full `.text` blocks, decode every opcode with `Lifter`, and verify **0 unhandled instruction mnemonics**.

### B. NSO Binary Loader & ELF Relocation Engine
- **Current State:**
  - `src/runtime/module_loader.cpp` only supported loading pre-compiled `.so` / `.dll` chunks via `dlopen`.
  - It did not parse raw Nintendo `NSO0` segments or apply relocations directly to memory.
- **Target Gate:**
  - Implement `ModuleLoader::load_nso(const std::string &path, uint64_t base_addr)` to parse `NSO0`, decompress `.text`, `.rodata`, `.data` via LZ4, verify SHA-256 hashes, map them into `0x7100000000`, and apply `.rela.dyn` (`R_AARCH64_RELATIVE`, `R_AARCH64_GLOB_DAT`, `R_AARCH64_JUMP_SLOT`, `R_AARCH64_ABS64`, `DT_RELR`).

### C. Horizon HLE Kernel & IPC Services
- **Current State:**
  - `src/runtime/services_hle.cpp` had a minimal dispatch stub.
  - Real TotK v1.4.3 modules rely heavily on CMIF (`SFCI`/`SFCO`) requests, domain sub-objects, pointer buffers, and IPC handles.
- **Target Gate:**
  - Fully integrate Hipc/CMIF request unmarshalling and implement services:
    - `sm:` (service discovery, checking allowed list)
    - `set` (language, region)
    - `time:u` (clocks, snapshot)
    - `acc:u0` (offline user profiles, `UID 1`)
    - `apm` (performance configuration, CPU/GPU clock modes)
    - `appletOE` (common state, self controller, focus)
    - `audren:u` (audio renderer work buffer, audio devices)
    - `fsp-srv` (OpenRomFs, OpenSaveDataFileSystem, SetCurrentProcess)
    - `pl:u` (shared font storage)
    - `vi:u` (display service, display opening)
    - `nvdrv` (device open, ioctl)

### D. NVN Graphics API -> Vulkan 1.3
- **Current State:**
  - `hle_nvn_gen.cpp` intercepted only ~5 NVN functions (`nvnDeviceGetProcAddress`, `nvnDeviceInitialize`, `nvnQueueInitialize`, `nvnCommandBufferInitialize`).
- **Target Gate:**
  - Implement full 523 NVN API dispatch table with LP64 argument unpacking, routing commands to Vulkan 1.3 (`nvn_vk.cpp`) and signaling `nvnQueuePresentTexture` frame boundaries.

### E. Guest Boot-Trace Orchestration
- **Current State:**
  - `totk_recomp` only ran the ImGui Studio or `--smoke-test`.
- **Target Gate:**
  - Implement `--boot-trace` flag in `src/main.cpp` that loads the module chain (`rtld` -> `main` -> `subsdk0` -> `sdk`), executes guest initialization hooks (`_init_libc0`, `nnosInitialize`, `_init_libc1`, `nninitInitializeSdkModule`, `nninitStartup`, `_init_libc2`), dispatches `nnMain`, and traces all SVC and IPC calls until the first frame presentation.
