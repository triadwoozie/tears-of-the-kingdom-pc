# Tasks for PC-2 (Secondary Graphics, Asset Pipeline & UI Node)

Welcome PC-2! The primary node (PC-1) is actively driving `--boot-real` past module loading, guest threading (24 active Horizon threads), SEAD allocator trees, and material descriptors into `gsys::NVNMgr`.

Please pick up and implement the following self-contained tasks in order. All tasks must build cleanly with `ninja -C build -j 2` and pass `ctest --test-dir build --output-on-failure`.

---

## Task PC2-1: Vulkan 1.3 Command Buffer Recording & Queue Presentation
- **Target Files:** `src/graphics/nvn_vk.cpp`, `src/graphics/nvn_vk.h`, `src/graphics/hle_nvn_gen.cpp`
- **Scope:**
  - Implement full Vulkan 1.3 command recording in `nvnCommandBufferDrawArrays`, `nvnCommandBufferDrawElements`, `nvnCommandBufferBindVertexBuffer`, `nvnCommandBufferBindUniformBuffer`.
  - Implement uniform buffer ringbuffer updating via `nvnCommandBufferUpdateUniformBuffer` using host-visible staging memory or push descriptors.
  - Implement `nvnCommandBufferSetViewport`, `nvnCommandBufferSetScissor`, and dynamic render pass color clear (`nvnCommandBufferClearColor`).
  - Wire `nvnQueuePresentTexture` to acquire the next swapchain image, transition layout from `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` to `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`, submit command buffer, and present via `vkQueuePresentKHR`.
  - Maintain compatibility with `tests/test_nvn_vulkan.cpp`.

---

## Task PC2-2: Fast CPU/Compute ASTC Texture Decompression Helper
- **Target Files:** `src/graphics/astc_decoder.cpp`, `src/graphics/astc_decoder.h`, `tests/test_astc_decoder.cpp`
- **Scope:**
  - Desktop GPUs (AMD, Nvidia GeForce) lack hardware ASTC texture decoding present on Tegra X1 / Apple Silicon.
  - Implement an ASTC block decoder supporting 4x4, 5x5, 6x6, 8x8, 10x10, and 12x12 footprint blocks, decoding uncompressed ASTC LDR blocks into standard RGBA8 (`VK_FORMAT_R8G8B8A8_UNORM`).
  - Integrate SIMDe NEON/SSE vectors for parallel pixel reconstruction.
  - Add unit test `tests/test_astc_decoder.cpp` testing synthetic 4x4 and 8x8 color blocks.

---

## Task PC2-3: Dear ImGui Studio Diagnostics & Config Serialization
- **Target Files:** `src/ui/studio.cpp`, `src/ui/studio.h`, `src/ui/overlay.cpp`, `tests/test_ui_config.cpp`
- **Scope:**
  - Implement the **Boot Trace & Symbol Inspector** tab in Dear ImGui Studio displaying active guest threads (`ActiveGuestThread`), program counters, and recent trace log ringbuffer.
  - Implement complete serialization/deserialization for `local/config.toml` (resolution scaling, Vulkan device index, VSync mode, audio volume, controller keybindings).
  - Add unit test `tests/test_ui_config.cpp` verifying config saving, loading, and fallback defaults.

---

## Task PC2-4: Transparent Zstandard (.zs) Decompression Helpers
- **Target Files:** `src/runtime/filesystem_hle.cpp`, `src/runtime/filesystem_hle.h`, `tests/test_fs_ipc.cpp`
- **Scope:**
  - Tears of the Kingdom heavily compresses RomFS archives using Zstandard with custom dictionaries (`ZsDic.pack.zs`).
  - Implement `ZstdDecompressor` class wrapping standard Zstd frame decompression and dictionary initialization.
  - Ensure `RomFS` virtual mount can transparently decompress `.zs` files upon `nn::fs::OpenFile` / `ReadFile`.
