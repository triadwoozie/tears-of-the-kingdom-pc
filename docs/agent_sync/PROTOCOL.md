# Multi-PC Relay Protocol — Distributed TotK-Recomp Development

## 1. Node Roles & Specializations

### PC-1: Primary Runtime, Recompiler & Boot Node (Current Node)
- **Environment:** Has local game dumps (`local/exefs/`), generated C chunks (`work/gen/`), and live AArch64 lifting infrastructure.
- **Responsibilities:**
  - Execute `./build/totk_recomp --boot-real` iterative boot loop.
  - Implement low-level Horizon kernel primitives (`svcMapMemory`, `svcArbitrateLock`, `svcSignalToAddress`), AArch64 lifted dispatch, and SEAD allocator runtime hooks.
  - Diagnose memory faults, register crashes, and indirect dispatch targets.
  - Commit active boot telemetry and dispatch tasks to PC-2.

### PC-2: Secondary Graphics Engine, Asset Pipeline & UI Node
- **Environment:** Clean Git clone of `https://github.com/triadwoozie/tears-of-the-kingdom-pc` without proprietary ROMs or `work/gen/` chunks.
- **Responsibilities:**
  - Pure C++20 / Vulkan 1.3 implementations that build cleanly via CMake & Ninja without needing game dumps.
  - NVN command recording and Vulkan graphics pipeline generation (`src/graphics/nvn_vk.cpp`, `src/graphics/hle_nvn_gen.cpp`).
  - ASTC texture decoding for non-ASTC desktop GPUs (`src/graphics/astc_decoder.cpp`).
  - ImGui Studio & Overlay diagnostics (`src/ui/studio.cpp`, `src/ui/overlay.cpp`).
  - Filesystem transparent Zstandard decompression (`src/runtime/filesystem_hle.cpp`).
  - Accompanying unit tests in `tests/` for all new features.

---

## 2. Synchronization Workflow via Git

1. **Task Assignment:**
   - PC-1 specifies tasks in `docs/agent_sync/TASKS_FOR_PC2.md` and pushes to `main`.
2. **Work Execution by PC-2:**
   - PC-2 pulls `main`: `git pull --rebase origin main`.
   - PC-2 picks an open task, implements the code in `src/` and unit test in `tests/`.
   - PC-2 runs `ninja -C build -j 2 && ctest --test-dir build --output-on-failure`.
   - PC-2 updates `docs/agent_sync/STATUS_PC2.md`.
3. **Commit & Push:**
   - PC-2 commits: `git commit -m "feat(subsystem): implement Task PC2-X"`
   - PC-2 pushes to `main` (or `pc2-worker` branch): `git push origin main`.
4. **Integration by PC-1:**
   - PC-1 runs `git fetch origin` every 5 boot iterations, rebases or merges PC-2's contributions, and incorporates them into `--boot-real`.
