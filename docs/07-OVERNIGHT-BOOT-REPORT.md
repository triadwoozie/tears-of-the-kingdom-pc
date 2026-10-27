# Overnight Boot Report

## Iteration 183

- **Build:** `ninja -C build -j 2` succeeded; no build work was pending.
- **Tests:** `ctest --test-dir build -j 2 --output-on-failure` passed all 10 tests.
- **Boot:** `./build/totk_recomp --boot-real` ran for the 25-second bound and exited via timeout (124), not a clean application shutdown.
- **Runtime snapshot:** The watchdog reported 369,756,812 calls at approximately 21.9 million calls per second and 11 active Horizon threads. MainThread remained at `PC=0x7104af51a4`, `LR=0x7104bfe1b4`; `0x7104be5830` is present in its stack.
- **Static finding:** `f_7104af51a4` repeatedly loads the link at offset `+0x18`, increments a counter, and exits only when that link is null. This supports a linked-list traversal diagnosis, not a display/vsync synchronization diagnosis. The other two addresses are return sites, not standalone function entries.
- **Action:** Added `tools/local_loop_scanner.py` for bounded local inspection and symbol cross-referencing. No runtime hooks were added because bypassing the traversal without understanding the list invariant would risk skipping game initialization.
- **PC-2:** `origin/pc2-worker` is already integrated into `main`; the fetched status reports no active relay task. This trace did not establish a new graphics, shader, or archive requirement.

## Post-Integration Checks (Iterations 184-185)

- **PC-2 update:** Integrated `d8662ba`, adding BNSH/fallback shader pipeline support, GameFramework progression hooks, multi-dictionary Zstd loading, and expanded ASTC support.
- **Build/tests:** Reconfigured and rebuilt successfully with `ninja -C build -j 2`; all 10 CTests passed.
- **Boot regression:** Two bounded boot runs exited 139 with segmentation faults rather than timing out. Both logs show `DIAG VAbortImpl` and result `0x5299` (Module 153, Description 41). Guest fault PCs differed: `0x7104c91b54` and `0x710c0ce5d0`.
- **Filesystem clue, not confirmed cause:** Both logs show `fsp-srv` command 1005 near the failure. The available traces do not establish causation or the meaning of the result code.
- **Follow-up:** Added PC2-12 to investigate the boot-time filesystem/result interaction, determine the result mapping, and create a targeted regression test. No speculative runtime bypass was added.
