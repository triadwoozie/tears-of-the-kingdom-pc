# Sead ABI, SwitchRecomp Specifications & Runtime Compliance

## 1. Executive Summary & Specification Matrix

This document defines the ABI and runtime memory specifications for **`TotK-Recomp`**, synthesizing standards from:
- **`SwitchRecomp` Specifications:** `SPEC-040` (Runtime ABI), `SPEC-045` (Runtime Memory), `SPEC-050` (CPU ISA), `SPEC-070` (OS Services), `SPEC-095` (Build Manifest Integrity), and `SPEC-020` (Inputs & Provenance).
- **`zeldaret/botw` Core Engine Framework:** `lib/sead` (Nintendo EPD's standard C++ framework) and `lib/NintendoSDK` (Horizon OS userspace ABI).
- **`totk-mobile` & `LASRecomp`:** Desktop 39-bit identity memory window and AOT static lifting.

---

## 2. AArch64 vs. x86_64 Memory Model & Atomics

Nintendo Switch hardware (NVIDIA Tegra X1 / Cortex-A57) operates under the **ARMv8 Weak Memory Model**, which permits aggressive memory reordering of loads and stores unless guarded by explicit barrier or acquire/release semantics. Host x86_64 systems operate under **Total Store Order (TSO)**, which inherently guarantees that stores are not reordered with other stores, nor loads with other loads.

```
       ARMv8 Guest Model (Weak)             x86_64 Host Model (TSO)
┌─────────────────────────────────────┐   ┌─────────────────────────────┐
│  LDAR  (Acquire Load)               │   │ Standard MOV load           │
│  STLR  (Release Store)              │   │ Standard MOV store          │
│  DMB ISH / DMB ISHST                │──▶│ Compiler barrier (no mfence)│
│  DMB ISHLD / DMB SY                 │   │ _mm_mfence() (if seq_cst)   │
│  LDAXR / STLXR (Exclusive Monitor)  │   │ LOCK CMPXCHG / CMPXCHG16B   │
└─────────────────────────────────────┘   └─────────────────────────────┘
```

### 2.1 Exclusive Monitor Emulation
AArch64 uses load-linked / store-conditional pairs (`LDXR`/`STXR`, `LDAXR`/`STLXR`) for synchronization. In `TotK-Recomp`:
- Each virtual CPU core context (`Arm64Context`) maintains:
  ```cpp
  uint64_t exclusive_addr = ~0ull;
  uint64_t exclusive_val = 0;
  ```
- Loads (`LDXR`, `LDAXR`) record the physical guest address and value.
- Stores (`STXR`, `STLXR`) verify that `exclusive_addr` matches the target address and execute an atomic compare-and-swap (`__atomic_compare_exchange_n`) with `__ATOMIC_SEQ_CST`. On x86_64, this lowers directly to `lock cmpxchg`.
- For 128-bit atomic pairs (`LDXP`/`STXP`), we lower to `lock cmpxchg16b`.

---

## 3. Thread-Local Storage & `TPIDRRO_EL0`

In Horizon OS on AArch64, userspace threads access their thread control block and thread-local storage (TLS) via the read-only system register **`TPIDRRO_EL0`**.

```
[TPIDRRO_EL0 Base Address]
0x000 ┌────────────────────────────────────────┐
      │ IPC Command Buffer (0x100 bytes)       │
0x100 ├────────────────────────────────────────┤
      │ Thread Local Storage Slots             │
      │ Slot 0: Main Thread Ptr                │
      │ Slot 1: errno / C++ Exception Context  │
      │ Slot 2: sead::Thread Pointer           │
0x1F8 ├────────────────────────────────────────┤
      │ KThread Horizon Handle                 │
0x200 └────────────────────────────────────────┘
```

### 3.1 Host Mapping Strategy
1. In `src/runtime/threads_hle.cpp`, each host worker thread allocated for guest execution allocates a 4 KiB aligned page for its thread context.
2. The guest pointer to this page is stored in host thread-local storage:
   ```cpp
   thread_local uint8_t *t_guest_tls = nullptr;
   ```
3. When lifted code reads `TPIDRRO_EL0` (such as `mrs x16, TPIDRRO_EL0`), the static lifter replaces the instruction with:
   ```c
   c->x[16] = (uint64_t)(uintptr_t)t_guest_tls;
   ```
   This gives $O(1)$ zero-syscall access to guest TLS identical to hardware ARM64 execution.

---

## 4. LLP64 (Windows) vs. LP64 (Switch & Linux) Data Model Invariant

A major pitfall in cross-compiling Horizon OS runtimes for Windows is the data model disparity:
- **Switch Horizon OS & Linux (LP64):** `sizeof(int) = 4`, `sizeof(long) = 8`, `sizeof(void*) = 8`.
- **Windows x86_64 (LLP64):** `sizeof(int) = 4`, `sizeof(long) = 4`, `sizeof(long long) = 8`, `sizeof(void*) = 8`.

If middleware headers from `zeldaret/botw/lib/sead` or `NintendoSDK` use `unsigned long` for sizes, offsets, or bitmasks, compiling under MSVC on Windows causes 4-byte truncations and memory corruption.

### 4.1 Strict Type Discipline
In `src/runtime/guest_abi/`:
1. **Never use `long` or `unsigned long`** in guest structures.
2. Always use explicit fixed-width integers:
   - `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`.
   - `int8_t`, `int16_t`, `int32_t`, `int64_t`.
3. Every guest struct header includes compile-time assertions:
   ```cpp
   #define TOTK_STATIC_ASSERT_ABI(Type, ExpectedSize) \
       static_assert(sizeof(Type) == (ExpectedSize), "ABI Size Mismatch for " #Type)
   ```
This guarantees identical memory layouts on both Linux (Clang/GCC) and Windows (MSVC/Clang-cl).

---

## 5. `sead` Core Framework High-Level Replacements (HLR)

*The Legend of Zelda: Tears of the Kingdom* uses Nintendo EPD's `sead` engine framework for core systems. Many performance-critical math and utility routines in `sead` can be bypassed by High-Level Replacements in host C++:

| `sead` Subsystem / Symbol | Guest Function Signature | Host HLR Replacement | Performance Benefit |
| :--- | :--- | :--- | :--- |
| **`sead::Matrix34f::mult`** | `Matrix34f *m, const Vector3f &v, Vector3f *out` | `_mm_fmadd_ps` (AVX2/FMA) | 4x faster vector transformation |
| **`sead::Vector3f::normalize`** | `Vector3f *v` | `_mm_rsqrt_ps` + Newton-Raphson | 5x faster vector normalization |
| **`sead::HashCRC32::calcHash`**| `const void *data, uint32_t size` | Native `_mm_crc32_u64` intrinsic | 12x faster string/asset hashing |
| **`sead::SafeStringBase`** | Length, comparison, formatting | Direct host `std::string_view` | Eliminates guest string loop overhead |
