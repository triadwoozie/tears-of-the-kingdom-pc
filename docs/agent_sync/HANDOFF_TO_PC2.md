# Direct Handoff & Autonomous Orchestration Directives for PC-2

## 1. System Status & Node Roles
- **PC-1 (Local Node):** Entering **Standby Relay Mode**. PC-1 will monitor GitHub, pull completed commits, check system health, and relay high-level progress to the user without consuming heavy quota.
- **PC-2 (Secondary Node):** Promoted to **Lead Autonomous Engineering Node**. PC-2 takes over all heavy lifting, boot loop progression, asset pipeline bring-up, and graphics driver completion.

---

## 2. The User's Core Mission & Non-Stop Rule
> **"dont stop at ITERATION 160 keep working until assets are loaded in and the game boots up, give reference repos to pc 2 if needed too"**
> **"orchestrate multiple agents, give batches of work to copilot and opencode, check quotas, push in batches"**

PC-2 must NOT stop at Iteration 160. Continue driving execution forward until `gsys::NVNMgr` initializes, RomFS assets (`.zs` / `.pack` / `.sarc`) decompress and mount, textures load into the ASTC/Vulkan pipeline, and the game enters sustained frame presentation via `nvnQueuePresentTexture`.

---

## 3. Reference Repositories
Consult these repositories (already located in `reference_repos/`) for exact structures, ABIs, and algorithms:
1. **`reference_repos/totk-mobile`**:
   - `src/runtime/` & `src/graphics/`: Native Apple Silicon / Metal / Vulkan engine implementation for TotK.
   - Demonstrates exact resource managers, SEAD game framework structures, memory heaps, and NVN presentation loop.
2. **`reference_repos/SavageWindRecomp`**:
   - Clean implementation of Horizon OS synchronization, audio renderer IPC, and thread scheduling.
3. **`reference_repos/SwitchRecomp`**:
   - High-level emulation specifications (`specs/SPEC-001` through `SPEC-095`).
   - Exact LP64 structs for `nn::hid`, `nn::vi`, `nn::fs`, `nn::os`, and `nvn`.
4. **`reference_repos/LASRecomp`**:
   - Recompiled function table dispatch architecture, register context restoration, and indirect branch recovery.
5. **`reference_repos/botw`**:
   - Breath of the Wild decompilation (`botw64`). Perfect structural reference for Nintendo's internal `gsys::*`, `agl::*`, and `sead::*` classes.

---

## 4. Multi-Agent Orchestration Guidelines for PC-2
1. **Sub-Agent Delegation:**
   - Offload boilerplate HLR hooks and isolated modules to local CLI agents (`copilot` and `opencode`).
   - For `opencode`, use the free cloud models:
     - `opencode/deepseek-v4.1-flash-free`
     - `opencode/nemotron-3-ultra-free`
     - `opencode/glm-5.3-flash-free`
2. **Verification Gate Before Every Commit:**
   - Run `ninja -C build -j 2` (never use more than `-j 2` to preserve host memory).
   - Run `ctest --test-dir build --output-on-failure`. All 10 tests must pass 100%.
3. **Batch Pushing:**
   - Commit atomic logical changes locally.
   - Push to `origin/main` or `origin/pc2-worker` in batches (every 3–5 commits or after major milestones).
4. **Boot Execution Guardrail:**
   - Always run the boot trace bounded by a timeout: `timeout 15s ./build/totk_recomp --boot-real`.

---

## 5. Current Execution State (Iteration 140)
- **Active Threads:** 23 concurrent Horizon threads active and communicating via `svcWaitForAddress` / `svcSignalToAddress`.
- **Completed Milestones:**
  - `_init_libc0` and `nnMain` entered.
  - SEAD heaps and memory manager initialized.
  - 300+ NVN methods bound via `NvnBridgeResolver`.
  - Format blocks, relocation tables, and resource descriptor trees initialized.
  - Container accessors (`0x71049b7c88`, `0x71049b7fb8`, `0x71049b8030`, `0x71049b80a8`, `0x71049b8120`) guarded with safe container nodes.
- **Next Engineering Objectives:**
  1. Complete Task PC2-4: Transparent Zstandard (`.zs`) decompression virtual RomFS mounting.
  2. Implement Task PC2-5: Dynamic NVN Render Target Allocation & Swapchain Blit.
  3. Implement Task PC2-6: Fast RomFS SARC/Pack file indexer.
  4. Drive `--boot-real` into `gsys::NVNMgr` render pass loop!
