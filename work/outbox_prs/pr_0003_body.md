### Summary of Changes

This pull request introduces bounds-checked memory operation overrides (`memcpy`, `memmove`, `__memmove_fast`, `__memmove_device`, and `memset`) into `src/runtime/dispatch.c` to protect against early boot boundary faults and underflowing pointer operations.

#### 1. Bounds Validation & Protection (`src/runtime/dispatch.c`)
- **Problem**: During early boot (heap pool sizing, font decompression, PLT linkage resolution), unaligned or unmapped memory dereferences can crash the host process before full initialization completes. Unchecked vector stores at uncommitted page margins trigger unrecoverable `SIGSEGV` faults.
- **Fix**:
  - Implemented `is_valid_guest_range(addr, size)` enforcing non-null/non-zero size validation, clamping absurd buffer sizes (`> 2 GiB`), and rejecting near-null accesses (`< 0x10000ULL`) as well as integer wrap-around.
  - Intercepted standard memory operations using `HLE_FUNC`: `memcpy`, `memmove`, `__memmove_fast`, `__memmove_device`, and `memset`.
  - Registered direct guest PLT / SDK runtime entry point overrides (`0x710c5b23c0ull` and `0x7106bb7bd0ull`) in `dispatch_init`.
  - Rate-limited diagnostic warnings log the faulting registers and link address if an illegal operation is blocked.
  - Guaranteed `c->x[0]` (`dst`) is preserved across calls according to AArch64 ABI.

### Verification
- Tested with Clang 18 on Linux (`x86_64`) under `-Wall -Wextra` with zero warnings.
- Verified patch application with `--whitespace=error-all`.
