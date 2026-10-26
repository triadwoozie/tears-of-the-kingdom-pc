### Summary of Changes

This pull request ports `tools/build_xlate.py` and the shader translation utility toolset (`run_ryujinx_xlate.py`, `run_limited.py`, `run_eden_xlate.py`, `compare_spirv_opt.py`, and `validate_spirv.py`) for native execution on Linux.

#### 1. Native Linux Build for `ultrs-shaders` (`tools/build_xlate.py`)
- **Problem**: `build_xlate.py` hardcoded `-r win-x64` and output filename `ultrs-shaders.exe`, preventing the Ryujinx Maxwell-to-SPIR-V translator from building natively on Linux and causing Step 5 of setup to fail.
- **Fix**: Dynamically selected the Runtime Identifier (`linux-x64` on Linux, `osx-arm64`/`osx-x64` on macOS, `win-x64` on Windows), named the executable `ultrs-shaders` on POSIX systems, and marked it executable (`0o755`).

#### 2. Cross-Platform Process Pinned Execution & Priority Control
- **Problem**: `tools/run_ryujinx_xlate.py`, `tools/run_limited.py`, `tools/run_eden_xlate.py`, `tools/compare_spirv_opt.py`, and `tools/validate_spirv.py` unconditionally invoked `ctypes.windll.kernel32.SetProcessAffinityMask` and Windows creation flags (`CREATE_SUSPENDED | BELOW_NORMAL_PRIORITY_CLASS`), causing fatal `AttributeError` or `ValueError` crashes on Linux.
- **Fix**: Added platform detection; on POSIX systems, applied process priority adjustments via `os.nice(10)` and thread/core pinning via `os.sched_setaffinity` in process `preexec_fn` hooks, while preserving the Win32 API implementation when running under Windows.

### Verification
- Tested with Python 3.14 on Linux (`x86_64`).
- Validated patch application with `--whitespace=error-all`.
