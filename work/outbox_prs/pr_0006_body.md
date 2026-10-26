### Summary of Changes

This pull request adds end-to-end Linux and Steam Deck / SteamOS builder pipeline support to `tools/setup.py` and introduces `setup.sh`.

#### 1. Linux & Steam Deck Builder Support (`tools/setup.py` & `tools/toolchain.py`)
- **Problem**: `tools/setup.py` assumed a Windows environment, requiring Windows-only binaries in `deps/clang64/bin/*.exe` and `deps/python/python.exe`, and attempting to query/download Windows MSYS2 release archives from GitHub on missing tools.
- **Fix**:
  - Dynamically resolved binary names (`UltraTears` on Linux, `UltraTears.exe` on Windows; `ultrs-shaders` on Linux, `ultrs-shaders.exe` on Windows).
  - On Linux (`sys.platform.startswith('linux')`), bypassed the Windows MSYS2 download logic and verified host toolchain binaries (`clang`, `clang++`, `ninja`) and Python packages (`lz4`, `capstone`, `unicorn`), bootstrapping missing packages via `pip install` when needed.
  - Used `sys.executable` during checkout builds on Linux rather than expecting hardcoded `deps/python/python.exe`.
  - Replaced Windows-only process priority flags with POSIX `os.nice(10)`.

#### 2. Root Launcher Script (`setup.sh`)
- Introduced an executable root `setup.sh` mirroring `setup.bat`, enabling one-click or drag-and-drop terminal execution on Linux desktop and Steam Deck environments.

### Verification
- Tested syntax and dependencies under Python 3.14 / Linux x86_64.
- Verified patch application with `--whitespace=error-all`.
