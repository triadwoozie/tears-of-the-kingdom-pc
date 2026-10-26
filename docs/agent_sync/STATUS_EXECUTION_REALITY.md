# Execution Reality Snapshot

**Observed:** 2026-10-26 10:22 +05:30
**Source:** `work/boot_trace_latest.log`
**Method:** Text-marker counts and bounded log tails. The log does not record
every guest dispatch, so function-name counts describe emitted log mentions,
not total executed guest calls.

## Observations

1. **Logged function breadth and concentration:** 19 unique `f_710...` names.
   The most frequent were `f_71048cb3dc` (1,536 mentions),
   `f_7106a37c4c` (684), `f_7104957410` (32), and `f_71048f0d70` (4).
   The remaining top-ten entries appeared three times or fewer. This is a
   narrow, highly concentrated log sample; it cannot establish the complete
   execution breadth or prove an infinite guest loop.
2. **Filesystem and RomFS activity:** The tail contains repeated
   `sead::ResourceMgr::load` hooks returning dummy resources, followed by
   `hid`, `audren:u`, and `fsp-srv` service acquisition. No explicit file
   open/read, RomFS file load, or archive extraction marker appears in the
   selected tail.
3. **GPU activity:** The tail shows NVN procedure-address queries for event
   and command-buffer APIs. It contains no explicit command-buffer recording,
   draw, queue submission, or present marker.
4. **Execution assessment:** The trace has progressed through guest/resource
   initialization and SDK service setup, so it is not evidence of a process
   doing literally nothing. However, the narrow reported function names,
   dummy resource returns, and absence of actual file-read or rendering
   markers indicate that meaningful asset loading and frame rendering are not
   demonstrated by this log. Treat this as initialization-heavy or
   stub-dependent progress, not confirmed gameplay or a proven wait-loop.

## Exact quick-check results

```text
Unique function-name mentions: 19
Top mentions: 1536 f_71048cb3dc; 684 f_7106a37c4c;
              32 f_7104957410; 4 f_71048f0d70;
              3 f_7104c59790; 3 f_71048c3e3c
Filesystem tail: dummy ResourceMgr loads; hid, audren:u, and fsp-srv acquisition
GPU tail: nvnDeviceGetProcAddress enumeration; no draw/submit/present marker
```

## Headless Resource/Vulkan Follow-up

**Observed:** 2026-10-26, after adding `--headless` and ResourceMgr filesystem
bindings.

- The offscreen Vulkan backend initialized successfully on the AMD Radeon 680M
  (`[Vulkan] Initialized Vulkan 1.3 headless offscreen backend successfully.`).
  No SDL window or swapchain was needed.
- ResourceMgr requests now resolve guest path strings and call
  `FileSystemHLE::read_romfs_file`, which uses the existing compressed-file and
  multi-dictionary Zstd path when applicable. This checkout has no
  `work/romfs` directory, so requests such as `RSDB/Tag.All.rstbl.byml` and
  `Pack/Bootup.Nin_NX_NVN.pack` could not be loaded; they correctly returned
  failure instead of fabricated file contents.
- The 20-second bounded boot did not reach its timeout: it exited 139 after
  missing-resource failures and an invalid indirect guest target. This is
  consistent with absent local RomFS data, but the exact guest crash cause is
  not established.
- The backend target and display-vsync event initialization were reached. The
  trace contains no evidence that the RenderBuffer hook advanced into command
  recording or that a frame was rendered.

## Guest Underflow Guard Follow-up

**Observed:** 2026-10-26, after the bounded ResourceMgr fallback and guest
address underflow guard changes.

- The earlier fault address, `0x6fffffffe0`, was 0x20 bytes below the guest
  memory base (`0x7000000000`). This is not an `acc:u0`/HID output-buffer or
  Vulkan null-device fault. The trace shows the fault while guest code was
  traversing a missing UI resource after ResourceMgr had returned null.
- Missing RomFS resources now return the initialized safe SEAD resource rather
  than a null pointer. Guest memory also reserves a zeroed page immediately
  below its base for small underflowing reads/writes.
- The next 20-second headless run reached the timeout (exit 124) without the
  previous SIGSEGV. Its tail was dominated by repeated audio-renderer IPC
  commands; this verifies survival past the prior crash, not successful
  resource loading or a rendered frame. `work/romfs` is still absent.
