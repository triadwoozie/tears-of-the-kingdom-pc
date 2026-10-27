# Status Report from PC-2 (Secondary Graphics/UI Node)

*This file will be updated by the secondary Antigravity instance running on PC-2.*

## Active Tasks
- None (All Relay Tasks PC2-1 through PC2-6 completed, verified 100% passing tests, and synced).

## Completed Tasks
- **Task PC2-1: Vulkan 1.3 Command Buffer Recording & Queue Presentation**
  - Implemented `VulkanBackend` command recording state and mock harness (`initialize_mock()`, `begin_command_recording()`, `end_command_recording()`).
  - Added recording methods: `cmd_draw`, `cmd_draw_indexed`, `cmd_bind_pipeline`, `cmd_bind_vertex_buffers`, `cmd_bind_index_buffer`, `cmd_set_viewport`, `cmd_set_scissor`, `cmd_clear_color`, `cmd_bind_uniform_buffer`, and `cmd_update_uniform_buffer`.
  - Wired NVN HLE entrypoints (`nvnCommandBufferDrawArrays`, `nvnCommandBufferDrawElements`, `nvnCommandBufferBindVertexBuffer`, `nvnCommandBufferBindUniformBuffer`, `nvnCommandBufferUpdateUniformBuffer`, `nvnCommandBufferSetViewport`, `nvnCommandBufferSetScissor`, `nvnCommandBufferClearColor`, and `nvnQueuePresentTexture`) to the active `VulkanBackend`.
  - Added image layout transition to `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` and swapchain queue presentation via `vkQueuePresentKHR`.
  - Added unit test `test_vulkan_command_recording_and_presentation` to `tests/test_nvn_vulkan.cpp`. All 8 CTests pass 100%.

- **Task PC2-2: Fast CPU/Compute ASTC Texture Decompression Helper**
  - Implemented `src/graphics/astc_decoder.h` and `src/graphics/astc_decoder.cpp` supporting 4x4, 5x5, 6x6, 8x8, 10x10, and 12x12 footprint blocks, decoding uncompressed ASTC LDR blocks into standard RGBA8 (`VK_FORMAT_R8G8B8A8_UNORM`).
  - Integrated SIMDe SSE2/NEON vector arithmetic for parallel 4-pixel channel blending.
  - Hooked `AstcDecoder::decompress_image` into `NvnTextureCache::decompress_astc_to_rgba8`.
  - Added unit test `tests/test_astc_decoder.cpp` testing synthetic 4x4, 8x8 void-extent color blocks, linear gradients, and full 16x16 / 15x15 image decompression. All 9 CTests pass 100%.

- **Task PC2-3: Dear ImGui Studio Diagnostics & Config Serialization**
  - Implemented the **Boot Trace & Symbol Inspector** tab in Dear ImGui Studio displaying active guest threads (`ActiveGuestThread` ID, name, PC, SP, priority, state, resolved symbol), TSV symbol table loader and offset resolver, and dynamic execution trace log ringbuffer viewer with auto-scroll.
  - Implemented complete, bidirectional serialization & deserialization for `local/config.toml` covering `[rom]`, `[graphics]`, `[audio]`, `[controller]`, and `[keybindings]`.
  - Upgraded in-game overlay HUD (`src/ui/overlay.cpp`) to display active Horizon threads and PC symbol.
  - Added unit test `tests/test_ui_config.cpp` verifying full config TOML round-tripping, fallback defaults, thread inspection, and TSV symbol lookup. All 10 CTests pass 100%.

- **Task PC2-4: Transparent Zstandard (.zs) Decompression Helpers**
  - Implemented `ZstdDecompressor` in `src/runtime/filesystem_hle.h` and `src/runtime/filesystem_hle.cpp` wrapping Zstd frame compression, frame decompression, and custom dictionary loading (`ZsDic.pack.zs`).
  - Wired `FileSystemHLE::read_romfs_file` to support transparent `.zs` lookup and decompression when guest requests files without extension.
  - Added offset and slice reading support for decompressed RomFS archives.
  - Added unit test `test_zstd_decompression_and_transparent_romfs` in `tests/test_fs_ipc.cpp`. All 10 CTests pass 100%.

- **Task PC2-5: Dynamic NVN Render Target Allocation & Swapchain Blit**
  - Implemented dynamic frame buffer and render pass creation and caching (`get_or_create_framebuffer`, `get_or_create_render_pass`) supporting up to 8 MRT color attachments and depth/stencil attachments.
  - Implemented `cmd_set_render_targets` and `cmd_set_render_targets_simple`, wiring `hle_nvnCommandBufferSetRenderTargets` directly to the active Vulkan backend.
  - Implemented HDR offscreen render target to SDR swapchain resolve and linear blit (`blit_hdr_to_swapchain`), supporting `VK_FORMAT_R16G16B16A16_SFLOAT` and `VK_FORMAT_A2B10G10R10_UNORM_PACK32` during presentation.
  - Added unit test `test_dynamic_render_targets_and_hdr_blit` in `tests/test_nvn_vulkan.cpp`. All 10 CTests pass 100%.

- **Task PC2-6: Fast RomFS SARC / Pack File Indexer**
  - Implemented `SarcArchive` in `src/runtime/filesystem_hle.h` and `src/runtime/filesystem_hle.cpp` parsing SARC headers, SFAT nodes, and SFNT string tables.
  - Implemented SARC hash function (`sarc_hash`) with hash lookup multiplier `0x65` for $O(1)$ file retrieval without disk seeks.
  - Added synthetic SARC archive generator (`SarcArchive::create_archive`) for packaging and testing.
  - Wired transparent nested RomFS resolution in `FileSystemHLE::read_romfs_file` supporting `romfs:/Pack/*.pack/*` and `.sarc/*` with transparent nested `.zs` decompression.
  - Added zero-copy partial file slice querying and streaming (`read_sarc_file_slice`) to avoid full-buffer copies when querying resource headers.
  - Added unit test `test_sarc_archive_indexing` in `tests/test_fs_ipc.cpp`. All 10 CTests pass 100%.

- **Task PC2-7: NVN Queue Submission & Frame Synchronization**
  - Implemented `queue_submit_commands`, `queue_finish`, and `queue_flush` in `VulkanBackend` and tracked recording state in `CommandRecordingState`.
  - Wired HLE entrypoints `nvnQueueSubmitCommands`, `nvnQueueFinish`, and `nvnQueueFlush` to `VulkanBackend`.
  - Added unit test `test_queue_submission_and_synchronization` in `tests/test_nvn_vulkan.cpp`. All 10 CTests pass 100%.

- **Task PC2-8: GameFrameworkNx Monotonic Timers & Event Progression Hooks**
  - Implemented `src/runtime/game_framework_hle.h` and `src/runtime/game_framework_hle.cpp` with `register_game_framework_hooks()`.
  - Registered monotonic 19.2 MHz hardware timer entry points: `nn::os::GetSystemTick`, `nn::os::GetSystemTickOrdered`, `nn::os::GetSystemTickFrequency`, `nnosGetSystemTick`, `nnosGetSystemTickOrdered`, and `nnosGetSystemTickFrequency`.
  - Wired non-blocking Horizon system event hooks (`nn::os::WaitSystemEvent`, `nnosWaitSystemEvent`, `nn::os::TryWaitSystemEvent`, `nnosTryWaitSystemEvent`, `nn::os::TimedWaitSystemEvent`, `nnosTimedWaitSystemEvent`, `nn::oe::GetNotificationMessageEvent`, and `nn::os::AttachReadableHandleToSystemEvent`).
  - Wired `nn::vi::GetDisplayVsyncEvent` in `src/main.cpp` with initialized auto-clear event handle preventing main loop deadlock.

- **Task PC2-9: NVN Surface Presentation & Non-Blocking NVNsync**
  - Wired `hle_nvnQueueFenceSync` and `hle_nvnSyncWait` in `src/graphics/hle_nvn_gen.cpp` returning `NVN_SYNC_WAIT_RESULT_ALREADY_SIGNALED` (0) without stalling render threads.
  - Connected `hle_nvnCommandBufferSetRenderEnableConditional` and `hle_nvnCommandBufferSetRenderEnable` to `NvnConditionalRenderManager`.
  - Added synthetic 10-Frame `GameFrameworkNx` Render Loop Simulation in `tests/test_nvn_vulkan.cpp` verifying window acquire, uniform buffer update, draw, submit, fence sync, and present across 10 consecutive frames.

- **Task PC2-10: Binary Nintendo Shader (.bnsh) Header Parser & Fallback SPIR-V Pipeline**
  - Implemented `src/graphics/bnsh_parser.h` and `src/graphics/bnsh_parser.cpp` with `validate_header`, `parse`, and `create_synthetic_bnsh`.
  - Embedded minimal valid SPIR-V 1.0 passthrough vertex and fragment shader bytecode (`s_fallback_vs_spv`, `s_fallback_fs_spv`).
  - Added `create_fallback_pipeline` ensuring `vkCreateGraphicsPipelines` produces valid pipeline handles.
  - Added unit test `test_bnsh_parsing_and_fallback_pipeline` in `tests/test_nvn_vulkan.cpp`.

- **Task PC2-11: Multi-Dictionary Zstd Pack Loading & Full ASTC Footprint Transcoding**
  - Upgraded `ZstdDecompressor` and `FileSystemHLE::load_romfs_dictionary` to support multi-dictionary SARC pack extraction (`ZsDic.pack.zs`) by ID and by filename (`decompress_with_named_dict`, `decompress_with_dict_id`).
  - Upgraded `AstcDecoder` and `NvnTextureCache` to support all Switch ASTC footprint dimensions (4x4 through 12x12) with sRGB-to-linear gamma conversion.
  - Added unit tests `test_zstd_multi_dictionary_pack` in `tests/test_fs_ipc.cpp` and `test_astc_all_supported_dimensions`, `test_astc_srgb_conversion` in `tests/test_astc_decoder.cpp`. All 10 CTests pass 100%.
