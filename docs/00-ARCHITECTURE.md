# TotK-Recomp Architecture Specification: Windows & Linux x86_64

## 1. Executive Architectural Overview

**`TotK-Recomp`** is a native static recompilation runtime and High-Level Emulation (HLE) environment designed to execute *The Legend of Zelda: Tears of the Kingdom* (v1.4.3) on modern 64-bit desktop platforms (x86_64 Windows and Linux).

Unlike traditional dynamic binary translation or just-in-time (JIT) emulators, `TotK-Recomp` combines:
1. **Ahead-of-Time (AOT) AArch64 Static Lifting:** Converting guest ARM64 instructions into readable, portable, optimization-friendly C/C++ representations compiled directly with Clang/Zig against host intrinsics.
2. **Direct 39-bit Zero-Overhead Virtual Memory Mapping:** Reserving the Switch's 39-bit address space (0x8000000 - 0x7FFFFFFFFF) directly within the 48-bit host virtual address space, eliminating guest-to-host address translation arithmetic.
3. **NVN-to-Vulkan 1.3 Graphics Engine:** Adapting the high-performance Nintendo NVN driver interface directly into native Vulkan 1.3 command buffers, pipeline caches, and timeline semaphores.
4. **Native SPIR-V Ingestion Pipeline:** Reusing the upstream offline Maxwell bytecode compiler (`Ryujinx.Graphics.Shader` / `dmsh_xlate`) to extract SPIR-V modules directly from RomFS/ExeFS, bypassing the intermediate Metal Shading Language (MSL) cross-compilation stage completely.

---

## 2. Component Mapping: `totk-mobile` vs. `TotK-Recomp`

The following matrix documents the architectural transition from Apple Silicon / Darwin / Metal to Desktop x86_64 / Vulkan 1.3 / Linux & Windows:

| Subsystem / Layer | `totk-mobile` (Reference: Darwin / iOS / macOS) | `TotK-Recomp` (Target: Windows / Linux x86_64) | Key Engineering Delta & Strategy |
| :--- | :--- | :--- | :--- |
| **Execution Model** | Binary rewrite + native ARM64 host execution | AOT Static Lifting (AArch64 -> C) via Capstone + Zig/Clang | AArch64 assembly lifted to C; compiled to native x86_64 machine code with AVX2/FMA vectorization. |
| **SIMD / Math** | Native Apple ARMv8.5 NEON execution | Portable C + Clang vector extensions + `SIMDe` (AVX2/FMA) | Bit-exact IEEE-754 / ARMv8 FPCR semantics (FZ, DN, Rounding) for Havok physics and `sead` engine. |
| **Memory Architecture**| Mach VM (`vm_allocate`, `vm_deallocate`) | Linux `mmap` (`MAP_NORESERVE`) & Win32 `VirtualAlloc` | Direct identity pointer mapping `(void*)(uintptr_t)addr` across the guest 39-bit window. |
| **Page Write Tracking**| `vm_protect` + Mach thread exception handler | Linux `userfaultfd` (`UFFD_FEATURE_WP_ASYNC`) & Win32 `GetWriteWatch` | Zero-signal kernel-level dirty page detection for Vulkan staging buffer flushes. |
| **Graphics API** | Apple Metal 3.1 / Metal 4 (`nvn_mtl.m`) | Vulkan 1.3 (`nvn_vk.cpp`) | Replaces Metal Render Command Encoders with Vulkan command buffers, descriptor sets, and pipeline layouts. |
| **Shader Backend** | SPIR-V -> SPIRV-Cross -> MSL -> `.metallib` | Offline Maxwell bytecode -> SPIR-V -> `VkShaderModule` | Direct SPIR-V ingestion! SPIR-V binary is saved into Vulkan Pipeline Cache; zero MSL conversion overhead. |
| **Windowing & Input** | AppKit (`graphics.m`) / UIKit (`main.m`) | **SDL3** (`SDL_CreateWindow`, `SDL_Gamepad`) | Cross-platform desktop windowing, high-DPI handling, raw gamepad input, and SDL3 Sensor API for gyro. |
| **Audio Subsystem** | CoreAudio / AudioUnit (`audio.c`) | SDL3 Audio Streams (`SDL_OpenAudioDeviceStream`) + Opus | Native software mixing into 48kHz float / PCM16 streams via SDL3 audio ringbuffer. |
| **Video Playback** | VideoToolbox / AVFoundation (`movie_video.c`) | Software NVDEC or FFmpeg H.264/VP9 decoder | Hardware-assisted or libavcodec video stream decode straight to NVN/Vulkan decoded YUV surfaces. |
| **UI & Overlays** | Native AppKit / SwiftUI panels | **Dear ImGui** (modern rounded dark theme) + SDL3 | Integrated pre-launch configuration studio and in-game `F1` / `Minus+Plus` overlay. |

---

## 3. NVN-to-Vulkan 1.3 Graphics Architecture

### 3.1 The `prepare_graphics.py` Adaptation Strategy
In `totk-mobile`, `script/prepare_graphics.py` acts as an AST/source transformer that adapts the baseline `core/gfx` driver code into `work/build/{hle_nv.c, hle_nvn.c, nvn_mtl.m}`. In `TotK-Recomp`, we refactor this pipeline into `tools/prepare_graphics_vk.py` producing `src/graphics/generated/nvn_vk.cpp`.

```
                  ┌──────────────────────────────────────────────┐
                  │    totk-mobile Baseline NVN State Machine    │
                  │   (host/nvn_*.inc, nvn_command_memory.inc)   │
                  └──────────────────────┬───────────────────────┘
                                         │
                                         ▼
                  ┌──────────────────────────────────────────────┐
                  │          tools/prepare_graphics_vk.py        │
                  │ (Extracts state tracking, binds, draw loops) │
                  └──────────────────────┬───────────────────────┘
                                         │
                     ┌───────────────────┴───────────────────┐
                     ▼                                       ▼
      ┌─────────────────────────────┐         ┌─────────────────────────────┐
      │   src/graphics/hle_nvn.c    │         │  src/graphics/nvn_vk.cpp    │
      │   - Command memory arena    │         │  - VkCommandBuffer recording│
      │   - Virtual pool allocator  │         │  - VkDescriptorSet updates  │
      │   - In-place jump thunks    │         │  - Dynamic rendering (1.3)  │
      └─────────────────────────────┘         │  - Pipeline cache management│
                                              └─────────────────────────────┘
```

### 3.2 Vulkan 1.3 Specific Features Leveraged
1. **Dynamic Rendering (`VK_KHR_dynamic_rendering`):** Eliminates rigid `VkRenderPass` and `VkFramebuffer` boilerplate. Render passes are opened directly with `vkCmdBeginRendering` matching NVN's dynamic color and depth-stencil target switching (`rt_same`, `zelda_cube_same`).
2. **Synchronization2 (`VK_KHR_synchronization2`):** Matches NVN's fine-grained barrier and fence semantics (`nvn_submit.inc`, `mtl_last_fence()`), allowing barrier splitting between draw calls.
3. **Timeline Semaphores:** Direct 1:1 emulation of NVN synchronization primitives (`NVNsync`, `mtl_wait`) without separate mutex-locked host CPU signaling.
4. **Buffer Device Address (`VK_KHR_buffer_device_address`):** Enables direct GPU pointer referencing inside uniform buffers and vertex buffers, mirroring NVN's unified virtual memory architecture (`c->v[n]`).
5. **Descriptor Indexing & Push Descriptors:** Maps NVN uniform and texture arrays (`p_bind_buf`, `p_bind_handles`) directly into Vulkan descriptor sets with minimum overhead.

---

## 4. Guest 39-Bit Virtual Memory Layout

Nintendo Switch Horizon OS 64-bit processes utilize a 39-bit virtual address space, corresponding to addresses up to $2^{39} - 1 = \text{0x7F\_FFFF\_FFFF}$ (512 GiB).

```
0x00_0000_0000 ┌─────────────────────────────────────────┐
               │ Reserved / Guard Page                   │ (0 - 128 MiB)
0x00_0800_0000 ├─────────────────────────────────────────┤
               │ Main Executable Image (NSO Text/Data)   │
               │ .text (RX), .rodata (R), .data/.bss (RW)│
0x00_8000_0000 ├─────────────────────────────────────────┤
               │ SubSDK0 & SDK Libraries                 │
0x70_0000_0000 ├─────────────────────────────────────────┤ GUEST_LO
               │ Dynamic Heap Arena                      │
               │ (sead::Heap, Havok memory pools)        │
0x71_0000_0000 ├─────────────────────────────────────────┤
               │ Thread Stacks & Guard Pages             │
0x72_0000_0000 ├─────────────────────────────────────────┤
               │ NVN GPU Memory Pools & Texture Buffers  │
0x73_2000_0000 ├─────────────────────────────────────────┤ GUEST_HI
               │ Extended Allocations & Temp Buffers     │
0x7F_FFFF_FFFF └─────────────────────────────────────────┘ Limit (512 GiB)
```

### 4.1 Address Translation Elimination
Because x86_64 user spaces on Linux and Windows 10/11 provide 47-bit (128 TiB) or 48-bit (256 TiB) virtual address ranges:
1. `TotK-Recomp` reserves the range `[GUEST_LO, GUEST_HI]` (`0x7000000000 - 0x7320000000`, 12.5 GiB) as an uncommitted address window at startup.
2. In full mode, it reserves `[0x8000000, 0x8000000000]` using `MAP_NORESERVE` on Linux or `MEM_RESERVE` on Windows.
3. Every guest memory access `LD8(addr)`, `LD32(addr)`, `ST64(addr, val)` compiles to:
   ```c
   #define LD32(a) (*(const uint32_t *)(uintptr_t)(a))
   #define ST32(a, v) (*(uint32_t *)(uintptr_t)(a) = (uint32_t)(v))
   ```
   **Zero CPU cycle loss** for address re-basing or bounds checking on standard memory instructions.

### 4.2 Page Write Tracking
For Vulkan buffer synchronization, `TotK-Recomp` detects guest CPU writes to memory-mapped textures and vertex pools:
- **Linux 6.7+:** `userfaultfd` with `UFFD_FEATURE_WP_ASYNC` and `PAGEMAP_SCAN` ioctls. The Linux kernel lifts the write protection asynchronously and records dirty pages without sending signals to user space.
- **Windows:** `VirtualAlloc(..., MEM_WRITE_WATCH, PAGE_READWRITE)` and `GetWriteWatch(WRITE_WATCH_FLAG_RESET, ...)`.

---

## 5. AArch64 SIMD / NEON Lowering to C + SIMDe (AVX2/FMA)

Tears of the Kingdom relies heavily on AArch64 Advanced SIMD (NEON) instructions across:
- **Havok Physics Engine:** Collision detection, ragdoll simulation, rigid body solvers, vehicle wheel physics.
- **Nintendo `sead` Core Library:** Vector4/Matrix44 transformations, frustum culling, bone skinning calculations.

### 5.1 Register & Execution State Model
The CPU state struct maintains 32 128-bit vector registers `V0-V31`:
```c
typedef struct Cpu {
    uint64_t x[31];
    uint64_t sp;
    uint64_t pc;
    uint32_t nzcv;
    uint32_t fpcr;
    uint32_t fpsr;
    uint64_t excl_addr;
    uint64_t excl_val;
    __uint128_t v[32];
} Cpu;
```

### 5.2 Clang Vector Extensions & SIMDe Lowering
Rather than emitting unoptimized scalar loops or slow software emulations, the static lifter decomposes ARM64 vector operations using Clang vector types and SIMDe:
```c
typedef float vf32 __attribute__((vector_size(16), may_alias));
typedef double vf64 __attribute__((vector_size(16), may_alias));
typedef int32_t vs32 __attribute__((vector_size(16), may_alias));

// Fused Multiply-Add (FMLA)
// ARM: fmla v0.4s, v1.4s, v2.4s
// C lowering:
static inline void fmla_4s(Cpu *c, int d, int n, int m) {
    vf32 vd = *(vf32 *)&c->v[d];
    vf32 vn = *(vf32 *)&c->v[n];
    vf32 vm = *(vf32 *)&c->v[m];
    *(vf32 *)&c->v[d] = simde_mm_fmadd_ps(vn, vm, vd);
}
```

### 5.3 IEEE-754 & FPCR Mode Fidelity
1. **Flush-to-Zero (`FPCR.FZ`):** When enabled by the game, subnormal inputs and results are clamped to zero. On x86_64, this is enabled natively in MXCSR (`_MM_FLUSH_ZERO_ON | _MM_DENORMALS_ZERO_ON`).
2. **Default NaN (`FPCR.DN`):** ARM generates a default quiet NaN (`0x7FC00000`) instead of propagating specific payload bits. The lifter canonicalizes NaNs when high precision math diagnostics are enabled.
3. **Reciprocal & Rsqrt Approximations (`FRECPE`, `FRSQRTE`):** Evaluated against ARM's hardware 8-bit lookup estimate tables (`simd_recip_tab`, `simd_rsqrt_tab`) to ensure deterministic game physics calculation identical to the Switch console.
