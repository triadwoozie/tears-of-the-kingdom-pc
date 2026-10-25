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

## Task PC2-4: Transparent Zstandard (.zs) Decompression Helpers [ACTIVE on PC-2]
- **Target Files:** `src/runtime/filesystem_hle.cpp`, `src/runtime/filesystem_hle.h`, `tests/test_fs_ipc.cpp`
- **Scope:**
  - Tears of the Kingdom heavily compresses RomFS archives using Zstandard with custom dictionaries (`ZsDic.pack.zs`).
  - Implement `ZstdDecompressor` class wrapping standard Zstd frame decompression and dictionary initialization.
  - Ensure `RomFS` virtual mount can transparently decompress `.zs` files upon `nn::fs::OpenFile` / `ReadFile`.

---

## Task PC2-5: Dynamic NVN Render Target Allocation & Swapchain Blit
- **Target Files:** `src/graphics/nvn_vk.cpp`, `src/graphics/nvn_vk.h`
- **Scope:**
  - Implement dynamic frame buffer attachment creation (`VkImageView`, `VkFramebuffer`) in `VulkanBackend` when `nvnCommandBufferSetRenderTargets` is called with multiple color textures and depth/stencil textures.
  - Implement swapchain blit / resolve from the offscreen HDR render target (`VK_FORMAT_R16G16B16A16_SFLOAT` or `VK_FORMAT_A2B10G10R10_UNORM_PACK32`) to the swapchain present image (`VK_FORMAT_B8G8R8A8_UNORM`) during `nvnQueuePresentTexture`.
  - Add unit test in `tests/test_nvn_vulkan.cpp`.

---

## Task PC2-6: Fast RomFS SARC / Pack File Indexer
- **Target Files:** `src/runtime/filesystem_hle.cpp`, `src/runtime/filesystem_hle.h`
- **Scope:**
  - In TotK, assets are packed into SARC archives (`.pack`, `.sarc`, `.byml`).
  - Implement SARC table of contents parser and hash lookup helper (`SarcArchive`) to quickly resolve sub-files without disk seeks.
  - Add unit test in `tests/test_fs_ipc.cpp`.
