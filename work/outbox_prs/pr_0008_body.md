### Summary of Changes

This pull request introduces a real-time diagnostics HUD overlay in the window title bar and modernizes the `tools/setup.py` CLI user experience with structured ANSI output.

#### 1. Real-Time Telemetry HUD (`src/host/window_sdl.c`, `src/host/svc.c`, `src/host/host.h`)
- **Problem**: When launching `UltraTears`, users and developers have limited visibility into runtime performance and initialization phases, making it difficult to distinguish asset loading from engine hangs.
- **Fix**:
  - Implemented real-time telemetry readout in the SDL3 window title bar: FPS, frame time (ms), active Vulkan GPU name (`vkc.props.deviceName`), allocated guest heap memory (`runtime_heap_bytes()`), and execution state (`Boot/Init` vs `Running`).
  - Added an interactive HUD toggle bound to `F3` (and controllable via environment variable `TOTK_HUD=1`).
  - Exported `runtime_heap_bytes()` in `src/host/svc.c` to report active heap footprint.

#### 2. Modernized Builder CLI UI (`tools/setup.py`)
- **Fix**:
  - Upgraded `tools/setup.py` terminal output with clear ANSI progress banners, step timers, and step status indicators.
  - Implemented structured error diagnostics boxes providing immediate actionable instructions when pre-flight checks fail.

### Verification
- Built under Clang 18 on Linux (`x86_64`) under `-Wall -Wextra` with zero warnings.
- Verified patch application with `--whitespace=error-all`.
