# TotK Shader Pipeline & NVN-to-Vulkan 1.3 Specification

## 1. Overview & Architectural Breakthrough

In *The Legend of Zelda: Tears of the Kingdom* (v1.4.3), shaders are written for the NVIDIA Maxwell Tegra X1 GPU architecture and stored as Maxwell bytecode blobs inside game programs and packages.

In `totk-mobile`, translating these shaders to Apple Metal required a complex, multi-stage pipeline:
```
Maxwell Bytecode ──▶ Ryujinx.Graphics.Shader ──▶ SPIR-V ──▶ SPIRV-Cross ──▶ MSL Source ──▶ Apple Metal IR (.metallib)
```

In **`TotK-Recomp`**, targeting Vulkan 1.3 on Windows and Linux x86_64 allows us to achieve a **direct SPIR-V bypass**:
```
Maxwell Bytecode ──▶ Ryujinx.Graphics.Shader ──▶ Validated SPIR-V (.spv) ──▶ VkShaderModule ──▶ VkPipelineCache
```

By intercepting the shader compilation at the SPIR-V stage, we completely eliminate:
1. The **SPIRV-Cross** translation pass.
2. The compilation of generated Metal Shading Language source code.
3. Apple-specific shader transformations (e.g., Apple Metal argument buffers, MSL attribute decorators).

We feed the SPIR-V directly into the host GPU's native Vulkan driver and write the resulting compiled monolithic graphics pipelines into a persistent `VkPipelineCache` on disk (`work/cache/vulkan_pipelines.bin`), virtually eliminating runtime shader compilation stuttering.

---

## 2. Upstream Pipeline Analysis (`totk-mobile/script/`)

### 2.1 Extraction: `script/extract_shaders.py`
Scans the game's RomFS and ExeFS images, extracting:
- Shader binary code payloads (Nvidia SASS instructions).
- NVN stage control headers (offsets, stage types: Vertex, Fragment, Compute, Geometry, Tessellation Control, Tessellation Evaluation).
- Hashes of each program blob.

### 2.2 Indexing & Variants: `script/index_shaders.py` & `script/shader_pairs.py`
- Indexes all extracted shader blobs into an authoritative index file (`work/shaders/index.json`).
- Analyzes variant masks:
  - For Vertex Shaders (`vs`): maps input attribute float/int/uint slot types (up to 16 slots).
  - For Fragment Shaders (`ps`): maps output target formats (up to 8 render targets).
- Identifies paired vertex-fragment shader interfaces to guarantee input/output interface matching between stages.

### 2.3 Translation: `script/translate_shaders.py`
Executes `dmsh_xlate.dll` (built on Ryujinx's `Ryujinx.Graphics.Shader`, `Ryujinx.Common`, and `Spv.Generator`) using the .NET runtime:
```python
result = subprocess.run([
    str(TOOLS / 'dotnet/dotnet'), 
    str(XLATE / 'dmsh_xlate.dll'), 
    str(job_file), 
    str(output)
])
```
Each job translates a Maxwell bytecode file into:
1. **`${KEY}.spv`:** The binary SPIR-V bytecode. Validated by ensuring:
   - Size $\ge 20$ bytes.
   - 32-bit Magic Header is `0x07230203` (in little-endian: `\x03\x02\x23\x07`).
2. **`${KEY}.res.json`:** Shader reflection metadata, documenting:
   - Constant / Uniform buffer bindings (`c0` through `c17`).
   - Storage buffer bindings (`sbufs`).
   - Sampled textures and samplers.
   - Thread group dimensions (`tg`) for compute shaders.

---

## 3. Feeding SPIR-V into Vulkan 1.3

### 3.1 Reflection Metadata Ingestion
The generated `${KEY}.res.json` metadata contains the exact binding layout required to construct Vulkan `VkDescriptorSetLayout`:

```json
{
  "key": "ec4f05739b0fbc91_b",
  "stage": "vs",
  "ubofs": [
    { "slot": 0, "size": 2048, "binding": 0 },
    { "slot": 1, "size": 65536, "binding": 1 }
  ],
  "sbufs": [
    { "cb_slot": 3, "cb_offset": 0, "binding": 2, "flags": 0 }
  ],
  "samplers": [
    { "index": 0, "binding": 3, "type": "2D" }
  ]
}
```

### 3.2 Descriptor Set Layout Generation
In `src/graphics/vk_pipeline_cache.cpp`:
1. For each stage (Vertex, Fragment, Compute), we parse the reflection metadata:
   - Uniform buffers (`ubofs`) $\rightarrow$ `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` or `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`.
   - Storage buffers (`sbufs`) $\rightarrow$ `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`.
   - Samplers $\rightarrow$ `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`.
2. Construct the `VkDescriptorSetLayout` with `VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR` to allow fast push descriptor updates without managing individual descriptor pools.

### 3.3 Shader Module Creation & Pipeline Warm-Up
```cpp
VkShaderModuleCreateInfo create_info = {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = spv_bytes_len,
    .pCode = reinterpret_cast<const uint32_t *>(spv_bytes)
};
VkShaderModule module;
vkCreateShaderModule(vk_device, &create_info, nullptr, &module);
```
During pre-launch warm-up, all shader combinations from `shader_pairs.json` are assembled into `VkGraphicsPipelineCreateInfo` structures and pre-compiled into `vk_pipeline_cache`. Subsequent launches load this binary cache in under 1 second.

---

## 4. NVN Graphics State to Vulkan 1.3 Mapping

`totk-mobile/host/` implements high-level graphics state handling in `host/nvn_*.inc`. The table below outlines how each component maps to Vulkan 1.3 in `TotK-Recomp`:

| Component / File | NVN / Metal Responsibility | Vulkan 1.3 Implementation (`src/graphics/nvn_vk.cpp`) |
| :--- | :--- | :--- |
| `nvn_command_memory.inc`| Command arena allocation, chunk rollover | Allocates `VkCommandBuffer` segments from thread-local `VkCommandPool`. |
| `nvn_conditional_draw.inc`| Query-based draw suppression | `VK_EXT_conditional_rendering` (`vkCmdBeginConditionalRenderingEXT`). |
| `nvn_inline_uniform.inc`| Fast uniform updates without pool alloc | `vkCmdPushConstants` for small uniforms; staging dynamic UBOs for large blocks. |
| `nvn_buffer_cache.inc` | Dirty range tracking for guest buffers | Asynchronous write tracking via `userfaultfd` (Linux) / `GetWriteWatch` (Win32) flushing to `VkBuffer` via staging copies. |
| `nvn_submit.inc` | Batching command buffers, split presents | `vkQueueSubmit2` with `VkSemaphoreSubmitInfo` tracking timeline semaphores. |
| `nvn_query_gpu.inc` | GPU timestamps, occlusion counters | `VkQueryPool` with `VK_QUERY_TYPE_TIMESTAMP` and `VK_QUERY_TYPE_OCCLUSION`. |
| `nvn_texture_alias.inc` | Handling render-to-texture aliases | `VkImageMemoryBarrier2` transitioning layouts (`COLOR_ATTACHMENT_OPTIMAL` $\leftrightarrow$ `SHADER_READ_ONLY_OPTIMAL`). |
| `nvn_color_clear.inc` | Target-specific color and depth clearing | Dynamic rendering `VkRenderingAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR` or `vkCmdClearAttachments`. |

---

## 5. FidelityFX Super Resolution (FSR) Upscaling Pipeline

Tears of the Kingdom uses dynamic resolution scaling internally, rendering between 720p and 900p in docked mode.
In `TotK-Recomp`:
1. The NVN presentation target is intercepted before the swapchain blit.
2. An AMD FSR 1.0 (EASU + RCAS) compute pass is dispatched:
   - **Edge-Adaptive Spatial Upsampling (EASU):** Scales the internal framebuffer to the target output resolution (1080p, 1440p, or 4K).
   - **Robust Contrast-Adaptive Sharpening (RCAS):** Restores high-frequency detail and sharp edges without halos.
3. The resulting image is presented directly to the SDL3 swapchain surface.
