### Summary of Changes

This pull request addresses Linux path sensitivity and configuration discovery issues in `src/host/boot.c` and `src/host/fs.c`.

#### 1. POSIX `ultratears.cfg` Loading & Working Directory Configuration (`src/host/boot.c`)
- **Problem**: `default_exefs()` previously enclosed the entire configuration file discovery and `ultratears.cfg` parser in `#ifdef _WIN32`. On Linux, the executable always defaulted to `data/exefs` without honoring custom `ultratears.cfg` settings, and never adjusted the working directory via `chdir()`.
- **Fix**: Added POSIX executable directory resolution via `readlink("/proc/self/exe")`, followed by POSIX directory change via `chdir(path)`, enabling proper `ultratears.cfg` parsing, custom RomFS/Shader paths, and save directory environment variable propagation on Linux and Steam Deck.

#### 2. Path Separator Normalization & Case-Insensitive RomFS Resolution (`src/host/fs.c`)
- **Problem**: RomFS lookup rejected paths containing backslashes (`\`) via `strchr(path, '\\')`, failing when guest components or configuration files passed mixed slash styles. Furthermore, Linux file systems are strictly case-sensitive, causing lookups to fail on subtle case discrepancies.
- **Fix**:
  - In `rom_path()`, normalized all backslashes to forward slashes before parsing directory segments.
  - In `file_index()`, added a secondary case-insensitive lookup (`strcasecmp`) if exact binary search fails, ensuring reliable file opening on case-sensitive Linux filesystems.

### Verification
- Compiled with Clang 18 on Linux (`x86_64`) under `-Wall -Wextra` with zero warnings.
- Verified patch application with `--whitespace=error-all`.
