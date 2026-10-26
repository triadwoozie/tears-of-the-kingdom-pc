### Summary of Changes

This pull request addresses execution blockers encountered during the game's transition from SEAD initialization into `agl` and `gsys` NVN rendering setup.

#### 1. Device Property Boundary Protection (`src/gfx/nvn/hle_nvn.c`)
- **Problem**: When querying device capability integers via `nvnDeviceGetInteger`, `agl` queries constants above `0x5F` (such as `0x60`, `0x61`). The previous implementation called `die("unsupported NVN device property %#x", w)` when querying properties outside `g_devinfo`, causing fatal engine termination.
- **Fix**: Replaced the fatal abort with a non-fatal warning log and returned `*out = 0`, allowing the graphics engine to gracefully identify unsupported features and fall back to standard rendering modes.

#### 2. Safe Dynamic Proc Address Stubs (`src/gfx/nvn/nvn_registry.c`)
- **Problem**: Dynamic procedure address lookups for checkpoint and vendor-specific debug functions (e.g., `nvnCommandBufferInsertCheckpointDynamic`, `nvnCommandBufferSetZCullZF32CompressionEnable`, `nvnDeviceSetAutomaticCheckpointsEnable`, `nvnDeviceSetCheckpointCallStacksEnable`) returned stubs registered with `NULL` callbacks via `hle_register(name, NULL)`. When called during command recording, `dispatch.c` terminated execution with `FATAL("unimplemented host function")`.
- **Fix**: Returned a safe, no-overhead no-op thunk (`nvn_nop_thunk`) for unknown dynamic procedure requests, enabling execution to advance uninterrupted into the frame presentation loop.

### Verification
- Tested compilation under Clang 18 on Linux (`x86_64`).
- Validated patch application with `--whitespace=error-all`.
