# PC-2 Handoff State

## Upstream pull requests

- PR #5, `feat/linux-build`, is open against `kuraeow/ultratears:dev`.
- PR #6, `fix/linux-memory-underflow-pr`, is open against `kuraeow/ultratears:dev`.
- PC-1 fork: `triadwoozie/ultratears`; PC-2 bridge: `triadwoozie/tears-of-the-kingdom-pc`.

## Crash trace

`f_800cb5e1c -> f_80006a920` (`sead::Heap::alloc`, 0x230 bytes) fails because
`c->x[1]`, the heap pointer associated with `f_802bb74c0` / TLS slot
`f_805143c60`, is `0`. The allocation returns `c->x[0] = 0`, and the caller
faults writing at `ST8(c->x[0] + 0x8, 0u)`.

## Heap-size verification update

The traced `svcSetHeapSize` call is SVC `0x01` at `0x805175598`, with
`x1 = 0x137e00000` (about 4.87 GiB). `plat_reserve_guest()` reserves the
480-GiB `[GUEST_LO, GUEST_HI)` virtual window. The low-memory layout reserves
6 GiB total, with the heap beginning 768 MiB into that region, leaving a
5.25-GiB heap span. The SVC now advertises a 6-GiB `APP_POOL`, caps the heap at
that actual 5.25-GiB span, accepts 4-KiB-aligned requests, checks the pool
without integer overflow, and handles a zero-size request by resetting the
heap and returning success plus `HEAP_START`.

The latest trace shows this request succeeds: `nninitStartup` returns, all
three module constructors complete, and NVN initializes a 1920x1080
two-image swapchain. The prior null-heap `SIGSEGV` at `f_800cb5e1c+0x88`
still occurs later; these SVC changes unblock startup but do not resolve that
separate crash.

Function-to-file index (function bodies are not copied into this bridge):

- `f_800cb5e1c`: `data/generated/main/main_554.c`
- `f_80006a920`: `data/generated/main/main_031.c`
- `f_802bb74c0`: `data/generated/main/main_1182.c`
- `f_805143c60`: `data/generated/sdk/sdk_020.c`
- `f_800c85f50`: `data/generated/main/main_547.c`
- `f_80092de78`: `data/generated/main/main_461.c`

## PC-2 patch ingestion

Put each patch and its PR body under `work/outbox_prs/`, then add a row to
`pr_manifest.tsv` with columns `branch_name`, `patch_file`, `pr_title`, and
`pr_body_file` (tab-separated; pipe-separated rows are also accepted). Use
`stack:<branch>` in the branch-name column to base a branch on `origin/dev`;
ordinary branch names are based on `upstream/dev`. PC-1 runs
`tools/ingest_pc2_prs.sh` to create and push each branch and open its PR.
