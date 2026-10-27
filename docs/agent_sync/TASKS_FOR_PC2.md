# Tasks for PC-2 (Secondary Graphics, Asset Pipeline & UI Node)

Welcome PC-2! The primary node (PC-1) is actively driving `--boot-real` past module loading, guest threading (23 active Horizon threads), SEAD allocator trees, format block descriptors, and material descriptors into `gsys::NVNMgr`.

Please pick up and implement the following tasks in order. All tasks must build cleanly with `ninja -C build -j 2` and pass `ctest --test-dir build --output-on-failure`.

---

## Reference Repositories for Consultation
PC-2 can consult the following reference repositories located in `reference_repos/` for exact Switch / Zelda engine patterns:
- **`reference_repos/totk-mobile`**: iOS/macOS static recompilation port of TotK v1.2.1 / v1.4.3. Excellent reference for NVN -> Metal / Vulkan mappings, resource managers, SEAD game framework structures, and RomFS loading.
- **`reference_repos/SavageWindRecomp`**: Open-source static recompilation runtime for Zelda games. Key reference for Horizon kernel syscall emulation, audio renderer, and thread scheduling.
- **`reference_repos/SwitchRecomp`**: SwitchRecomp runtime and specifications (`specs/SPEC-*.md`). Byte-exact LP64 ABIs for `nn::hid`, `nn::fs`, `nn::os`, `nn::vi`, and `nvn`.
- **`reference_repos/LASRecomp`**: Recompiler dispatch table architecture and static lifter chunk organization.
- **`reference_repos/botw`**: Breath of the Wild decompilation project (`botw64`). Perfect structural reference for `gsys::*`, `agl::*`, and `sead::*` class layouts.

---

## Task PC2-1: Vulkan 1.3 Command Buffer Recording & Queue Presentation [COMPLETED by PC-2]
- **Target Files:** `src/graphics/nvn_vk.cpp`, `src/graphics/nvn_vk.h`, `src/graphics/hle_nvn_gen.cpp`
- **Scope:** Completed in commit `f5d9e67`.

---

## Task PC2-2: Fast CPU/Compute ASTC Texture Decompression Helper [COMPLETED by PC-2]
- **Target Files:** `src/graphics/astc_decoder.cpp`, `src/graphics/astc_decoder.h`, `tests/test_astc_decoder.cpp`
- **Scope:** Completed in commit `b8e0bf1`.

---

## Task PC2-3: Dear ImGui Studio Diagnostics & Config Serialization [COMPLETED by PC-2]
- **Target Files:** `src/ui/studio.cpp`, `src/ui/studio.h`, `src/ui/overlay.cpp`, `tests/test_ui_config.cpp`
- **Scope:** Completed in commit `ad44e7d`.

---

## Task PC2-4: Transparent Zstandard (.zs) Decompression Helpers [COMPLETED by PC-2]
- **Target Files:** `src/runtime/filesystem_hle.cpp`, `src/runtime/filesystem_hle.h`, `tests/test_fs_ipc.cpp`
- **Scope:** Completed in commit `8afec16`.

---

## Task PC2-5: Dynamic NVN Render Target Allocation & Swapchain Blit [COMPLETED by PC-2]
- **Target Files:** `src/graphics/nvn_vk.cpp`, `src/graphics/nvn_vk.h`, `src/graphics/hle_nvn_gen.cpp`, `tests/test_nvn_vulkan.cpp`
- **Scope:** Completed. Dynamic framebuffer and render pass caching, multiple color render targets and depth attachment binding via `cmd_set_render_targets`, HLE dispatch in `nvnCommandBufferSetRenderTargets`, offscreen HDR target blit and swapchain resolve in `blit_hdr_to_swapchain` and `present_texture`. Unit tests verified in `tests/test_nvn_vulkan.cpp`.

---

## Task PC2-6: Fast RomFS SARC / Pack File Indexer [COMPLETED by PC-2]
- **Target Files:** `src/runtime/filesystem_hle.cpp`, `src/runtime/filesystem_hle.h`, `tests/test_fs_ipc.cpp`
- **Scope:** Completed. Full SARC table of contents parser (`SarcArchive`), SFAT hash lookup table (`sarc_hash`), SFNT string table resolver, zero-copy view and hash queries, synthetic archive builder (`create_archive`), and transparent nested RomFS resolution (`romfs:/Pack/*.pack/*` and `.sarc/*`) with transparent `.zs` decompressor integration. Unit tests verified in `tests/test_fs_ipc.cpp`.

---

## Task PC2-12: Investigate Iteration 184-185 Boot Abort and Filesystem Result
- **Evidence:** After integrating `d8662ba`, two bounded `--boot-real` runs exit 139. Both logs report `[DIAG VAbortImpl]` with result `0x5299` (Module 153, Description 41), and show `fsp-srv` command 1005 near the failure. Guest PCs differ (`0x7104c91b54` and `0x710c0ce5d0`), so the immediate fault site is not yet identified.
- **Scope:** Determine the meaning/source of result `0x5299`, inspect the command-1005 response and surrounding filesystem/archive flow, and establish whether either is causal. Add a focused regression test for the confirmed failure path.
- **Constraints:** Do not suppress `VAbortImpl` or return success for unknown filesystem failures. Keep changes localized to the confirmed subsystem and preserve guest-visible error reporting.
