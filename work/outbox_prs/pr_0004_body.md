### Summary of Changes

This pull request adds support for loose extracted RomFS directory hierarchies and safe fallback file handles in `src/host/fs.c`.

#### 1. Loose RomFS Directory & Environment Configuration (`src/host/fs.c`)
- **Problem**: When booting in developer workflows or testing environments where a monolithic `romfs.bin` image is absent, `runtime_fs_init` only checked `TOTK_ROMFS`. Furthermore, if the RomFS folder was empty or missing required assets during preliminary boot passes, `need(nwalked > 0, "RomFS folder is empty")` triggered an immediate fatal crash.
- **Fix**:
  - Checked `TOTK_ROMFS_DIR` environment variable prior to falling back to `TOTK_ROMFS` or default `data/romfs`.
  - Relaxed `index_folder` to log a warning rather than aborting when `nwalked == 0`, allocating a valid minimal dummy header (`TOTKRFS1`) to allow execution to proceed.

#### 2. Safe Zero-Filled Fallback Handles (`src/host/fs.c`)
- **Problem**: Incomplete asset directories can cause `romfs_open` to fail (`R_PATH_NOT_FOUND`), causing calling game subsystems to crash when dereferencing expected handle objects.
- **Fix**:
  - Introduced `ROM_FALLBACK_HANDLE` (`UINT64_MAX`).
  - When a requested file is unindexed or missing from a loose RomFS directory, `romfs_open` hands out a safe fallback handle and logs a non-fatal notification.
  - `romfs_read` on fallback handles safely zero-fills caller buffers via `guest_prepare_write` + `memset`, while `romfs_size` reports 0 and `romfs_close` safely cleans up handle state.

### Verification
- Tested compilation with Clang 18 on Linux (`x86_64`) under `-Wall -Wextra` with zero warnings.
- Verified patch application with `--whitespace=error-all`.
