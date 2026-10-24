# AGENTS.md — TotK-Recomp Engineering Guidelines & Verification Gates

## Project Mission & Scope
**`TotK-Recomp`** is a native static recompilation and High-Level Emulation (HLE) runtime for *The Legend of Zelda: Tears of the Kingdom* (v1.4.3), targeting 64-bit desktop environments (x86_64 Linux and Windows).

## Architectural Invariant
1. **Core Language & Toolchain:**
   - Runtime, lifted code, and SDL3/Vulkan UI are strictly written in **C11 / C++20**.
   - Build system is **CMake 3.20+ with Ninja** and **Zig 0.13+ (`build.zig`)**.
   - Primary compilers: **Clang 16+** (`clang` / `clang++`) and **MSVC 19.30+**.
   - Offline ingestion, lifting, and analysis scripts are in **Python 3.12+**.
   - **NO Rust or Cargo** inside production runtime or lifter code.
2. **Zero Proprietary Assets:**
   - Under no circumstances may proprietary binaries (NSOs, NCA, NSP, XCI), ROM dumps, keys (`prod.keys`, `title.keys`), shaders, or game fonts be committed to this repository.
   - Strict `.gitignore` must continuously enforce exclusion of `local/`, `work/`, `reference_repos/`, and all Nintendo Switch binary formats.

## Strict Verification Back-Pressure Gates
Every agent or engineer making modifications must adhere to the following continuous verification workflow:
1. **Compilation Cleanliness:**
   - Run `ninja -C build` after every structural or runtime edit.
   - Zero errors and zero warnings are permitted.
2. **Unit Test Coverage:**
   - Every new ABI structure (`src/runtime/guest_abi/`), memory management feature (`src/runtime/memory_manager.cpp`), lifter transformation (`src/recompiler/`), or kernel sync primitive (`src/runtime/kernel_hle.cpp`) **MUST** have an accompanying unit test in `tests/`.
   - Run `ctest --test-dir build --output-on-failure`. All tests must pass 100%.
3. **Cryptographic Integrity:**
   - Any ingested game module must be cryptographically recorded in `work/provenance.toml` following `SPEC-095` and `SPEC-020`.
   - The Pre-Launch UI (`Studio`) must accurately reflect provenance metadata in real-time.

## Project Structure
```text
TotK-Recomp/
├── AGENTS.md               # Engineering guidelines & verification gates
├── CMakeLists.txt          # Root CMake build configuration
├── build.zig               # Alternative Zig build pipeline
├── docs/                   # Architectural & stage specifications
├── tools/                  # Python 3.12+ preparation, ingestion, and static lifter
├── src/
│   ├── recompiler/         # AArch64 -> C lifter, register context & dispatch tables
│   ├── runtime/            # HLE Horizon kernel, IPC, memory manager, audio
│   │   └── guest_abi/      # Byte-exact LP64 structs (nn::hid, sead framework)
│   ├── graphics/           # NVN -> Vulkan 1.3 engine & SPIR-V pipeline cache
│   └── ui/                 # SDL3 + Dear ImGui Studio & In-Game Overlay
├── tests/                  # CTest test suite
├── third_party/            # SDL3, Dear ImGui, Volk, Vulkan-Headers, VMA, SIMDe
├── local/                  # [GITIGNORED] User ROMs and encryption keys
└── work/                   # [GITIGNORED] Extracted ExeFS, provenance.toml, SPIR-V cache
```

## Build and Test Commands
- Configure: `CC=clang CXX=clang++ cmake -B build -G Ninja`
- Compile: `ninja -C build`
- Run Test Suite: `ctest --test-dir build --output-on-failure`
