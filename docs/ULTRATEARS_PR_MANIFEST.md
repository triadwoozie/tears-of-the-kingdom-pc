# UltraTears 1.4.3 Linux Porting & Fix Manifest

This manifest documents all verified architectural fixes, boundary protections, HLE patches, and test harnesses discovered in `totk-desktop` (v1.4.3, Title ID `0100F2C0115B6000`), structured for direct upstream integration into [UltraTears](https://github.com/kuraeow/ultratears.git).

---

## 1. Core Memory Subsystem & Boundary Guards

### 1.1 Guest Memory Underflow Guard Page (`0x6fffffffe0`)
- **Problem:**
  During early SEAD allocator/heap pool initialization, downward stack allocations and signed negative pointer offsets occasionally underflow `0x7000000000` (e.g. `0x7000000000 - 0x20 = 0x6fffffffe0`).
- **Fix:**
  Commit `c5642b1`: Map a 64 KiB guard / scratch page at `0x6ffffff000ULL` immediately preceding `GUEST_MEMORY_BASE` (`0x7000000000ULL`) via `mmap(MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, PROT_READ | PROT_WRITE)`. This eliminates immediate SIGSEGV during negative relative indexing.

### 1.2 Bounds-Checked SDK & PLT `memcpy` Hooks
- **Addresses:**
  - `0x710c5b23c0` (SDK runtime `memcpy`)
  - `0x7106bb7bd0` (Main binary PLT `memcpy`)
- **Problem:**
  Raw recompiled memcpy loops dereference guest pointers without validating whether `dst` or `src` span committed pages or null singletons. Unchecked vector stores (`LD64` / `ST64`) at page boundaries cause unrecoverable page faults.
- **Fix:**
  Wrap both entry points with `is_mapped_guest_range()` checks and dispatch to host `std::memmove` when safe, preserving `X0 = dst` and returning cleanly to `X30`.

### 1.3 Memory Limit Expansion
- **Limit:** Expanded guest address window from `0x7320000000` (12.5 GiB) to `0x7400000000` (16 GiB) to accommodate full game heap reservations and large RomFS index mappings without address collision.

---

## 2. HLE Subsystems & Protocol Handshakes

### 2.1 Audio Renderer Protocol Handshake (`REV12` / `0x5299`)
- **Problem:**
  `audren:u` command 0 (`OpenAudioRenderer`) requires version handshake validation. v1.4.3 Switch OS uses revision protocol `0x5299` (`REV12`). Mismatched revision codes cause immediate abort with result `0x0000ea01` (`R_NOT_INITIALIZED`).
- **Fix:**
  Ensure the audio renderer initialization handshake in `audren:u` responds with `REV12` (`0x5299`) compatibility flags and services worker query loops (`0x710c1f5fd0`–`0x710c1f6ce0`).

### 2.2 VSync Event Signaling & Display Management
- **Hook:** `nn::vi::GetDisplayVsyncEvent` (`0x710c4504b0`).
- **Fix:**
  Auto-signal the guest display VSync event handle at 60 Hz to drive `sead::Framework::waitVsync` loops and advance the render thread into command buffer recording.

---

## 3. Offscreen Testing & Telemetry Infrastructure

### 3.1 `--headless` Offscreen Vulkan Flag
- **Implementation:**
  Allows full headless CI/CD execution and unattended debugging without requiring an active X11 / Wayland display server or physical window surface.
- **Offscreen Target:**
  Allocates an internal `VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT` Vulkan image and triggers `nvnQueuePresentTexture` frame ticks in memory.

### 3.2 Automated Watchdog & Boot Telemetry
- Thread-safe 1-second watchdog logger tracking:
  - Total recompiled indirect calls and call rate (achieving >21.7M calls/s).
  - Horizon thread pool states (`MainThread`, `Worker_0x20000`..`0x2000d`, `audio-renderer`).
  - Active stack frames and instruction pointer unwinding.

---

## 4. Test Suite Reference
Our standalone CTest validation harness covers:
1. `test_abi_structs`: Verifies LP64 struct alignments, offsets, and packing (`nn::hid`, `sead::FormatBlock`).
2. `test_memory_window`: Verifies guest memory reservations, bounds checking, and guard page allocation.
3. `test_lifter_neon`: Verifies AArch64 NEON SIMD vector translation to host x86_64 SIMD.
4. `test_kernel_sync`: Verifies Horizon kernel synchronization primitives (`svcWaitSynchronization`, `svcArbitrateLock/Unlock`).
5. `test_nvn_vulkan`: Verifies NVN state machine mapping to Vulkan 1.3 pipeline objects.
6. `test_audio_hid`: Verifies audio-renderer IPC serialization and controller input state polling.
7. `test_module_loader`: Verifies ExeFS NSO parsing and symbol table indexing.
8. `test_fs_ipc`: Verifies RomFS hierarchical directory lookups and file handle lifecycle.
9. `test_astc_decoder`: Verifies hardware and software ASTC texture decompression.
10. `test_ui_config`: Verifies window configuration and input mappings.
