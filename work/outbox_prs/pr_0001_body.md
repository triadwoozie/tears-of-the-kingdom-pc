### Summary of Changes

This pull request resolves a critical boot crash (`SIGSEGV` at `0x0000000000000008`) encountered during `nnMain` SEAD heap initialization (`f_800cb5e1c -> f_80006a920`), and hardens runtime memory allocation for Linux desktop recompilation.

#### 1. Worker Thread Guest TLS Propagation (`src/host/threads.c`)
- **Problem**: When `svcCreateThread` (`0x08`) spawns a guest worker thread, `guest_tls_alloc()` zero-initializes the thread's Horizon OS `tpidrro_el0` region. However, Nintendo SDK TLS slots (`+0x100` to `+0x180`) contain critical process singletons registered during early main thread setup, notably the root `sead::Heap` instance pointer (`f_805143c60` accessed by `f_802bb74c0`). When worker threads called `sead::Heap::alloc` (`f_80006a920`), `c->x[1]` evaluated to `0`, returning `0` and triggering an immediate `SIGSEGV` at `0x8` upon writing `c->x[0] + 0x8`.
- **Fix**: Recorded parent CPU's `tpidrro` and `tpidr` upon `svcCreateThread`, and in `thread_main` copied the 128-byte TLS slot window (`+0x100` to `+0x180`) from the parent thread into the newly allocated guest TLS block. Restored `c->tpidr = t->parent_tpidr`.

#### 2. Relax `svcSetHeapSize` Alignment & Expand Pool Limit (`src/host/svc.c`)
- **Problem**: `svcSetHeapSize` enforced strict 2 MiB alignment (`size % 0x200000`) and checked against a retail Switch memory pool of 3.2 GiB (`APP_POOL = 0xcd500000ULL`), returning `0xd401` on standard page-aligned 4 KiB requests.
- **Fix**: Relaxed alignment validation from 2 MiB to page alignment `(size & 4095) != 0`, and expanded `APP_POOL` to `(6ULL << 30)` (6 GiB) to accommodate full game heap reservations on 64-bit desktop hosts.

#### 3. Defensive Fallback Arena for `sead::Heap::alloc` (`src/host/guest.c`)
- **Fix**: Added an HLE override interceptor on `0x080006a920ull` (`sead::Heap::alloc`). In the event that an unattached thread or unit invocation requests allocation with `c->x[1] == 0`, a thread-safe 32 MiB fallback arena services the request and returns a valid zero-initialized memory region rather than returning NULL and faulting the process.

### Verification
- Tested with Clang 18 on Linux (`x86_64`).
- Validated patch application with `--whitespace=error-all`.
