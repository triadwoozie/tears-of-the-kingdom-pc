# Tears of the Kingdom PC Recompilation

TotK-Recomp is a native desktop recompilation and high-level emulation runtime
for *The Legend of Zelda: Tears of the Kingdom* version 1.4.3. The project
translates lifted AArch64 game code into host C and runs it alongside
implementations of selected Horizon OS, Nintendo SDK, graphics, audio, and
filesystem interfaces.

The project is under active development. A successful build and passing unit
tests do not imply that the full game boots or renders correctly; use the
bounded real-boot procedure below to check runtime progress.

## Project components

- `src/recompiler/` — guest CPU state, dispatch, and lifted-code integration.
- `src/runtime/` — memory management, kernel and service HLE, filesystem,
  audio, and game-framework support.
- `src/graphics/` — NVN-to-Vulkan support, shader handling, and texture
  processing.
- `src/ui/` — SDL3 and Dear ImGui Studio and overlay.
- `tools/` — Python ingestion/lifting tools and local operational scripts,
  including the remote-update boot runner.
- `tests/` — CTest-based unit tests.
- `third_party/` — vendored dependencies.

## Requirements

The primary supported build environment is 64-bit Linux with:

- CMake 3.20 or newer
- Ninja
- Clang 16 or newer (`clang` and `clang++`)
- Python 3.12 or newer for preparation and analysis scripts
- Vulkan development libraries and a compatible driver for graphics use

The project also targets Windows with a supported MSVC toolchain. Some SDL
features are optional and depend on which system development libraries are
installed.

## Configure and build

Configure with Clang and Ninja:

```sh
CC=clang CXX=clang++ cmake -B build -G Ninja
```

Build with limited parallelism:

```sh
ninja -C build -j 2
```

Run the unit tests:

```sh
ctest --test-dir build -j 2 --output-on-failure
```

## Real-boot smoke test

The runtime can attempt to load locally prepared game modules and execute
recompiled guest code:

```sh
timeout 25s ./build/totk_recomp --boot-real > work/boot_trace.log 2>&1
tail -n 30 work/boot_trace.log
```

Exit status `124` means `timeout` stopped the process at the requested bound;
it is not evidence of a completed boot. A crash, guest abort, or repeated
MainThread program counter should be treated as a runtime failure and
investigated from the trace. Do not interpret a passing unit-test suite as
proof of successful game execution.

## Automated remote-update runner

`tools/auto_runner.sh` is an optional local poller for a checkout on `main`.
It makes no LLM calls. Every 30 seconds it fetches `origin` and compares the
remote `main` commit with local `HEAD`. When they differ, the runner:

1. Rebases local `main` onto `origin/main`.
2. Runs `ninja -C build -j 2` and `ctest --test-dir build -j 2
   --output-on-failure`.
3. If both succeed, runs `--boot-real` for up to 25 seconds.
4. Extracts selected error, filesystem, NVN, and MainThread markers into
   `docs/agent_sync/LATEST_BOOT_STATUS.txt`.
5. Commits and pushes that telemetry to `origin/main`.

Build or test failure skips the boot and telemetry update. A rebase failure
stops the runner so a person can resolve the checkout before restarting it.
The boot result is recorded as an exit status; timeout status `124` is
expected for a bounded run and does not mean that the game completed booting.

Start it from the repository root:

```sh
nohup ./tools/auto_runner.sh > work/auto_runner.log 2>&1 &
echo $!
```

Check that it is running:

```sh
ps -p <PID> -o pid=,stat=,args=
tail -n 30 work/auto_runner.log
```

Stop it by sending `TERM` to the recorded process ID:

```sh
kill <PID>
```

The runner needs a configured Git remote, permission to fetch and push
`origin/main`, an initialized build directory, and local game input sufficient
for `--boot-real`. It pushes telemetry commits directly to `main`; run it only
when that behavior is appropriate for the checkout. Avoid running multiple
instances against the same worktree, since each instance may rebase, build,
commit, or push independently. Keep an eye on the worktree and telemetry
history while it is active.

## Local game data and repository hygiene

Game modules, ROM images, encryption keys, proprietary shaders, and game fonts
are not distributed by this repository and must not be committed. Keep
user-supplied data in ignored local/work directories and follow the project
`.gitignore` rules. Use only game data you are authorized to access.
