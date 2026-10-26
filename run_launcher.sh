#!/usr/bin/env bash
set -e

# UltraTears Desktop Launcher (Linux Runner)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

export PATH="$HOME/.cargo/bin:$HOME/.local/bin:$PATH"
export PKG_CONFIG_PATH="$HOME/.local/usr/lib64/pkgconfig:$HOME/.local/usr/lib/pkgconfig:$HOME/.local/usr/share/pkgconfig:$PKG_CONFIG_PATH"
export LD_LIBRARY_PATH="$HOME/.local/usr/lib64:$HOME/.local/usr/lib:$LD_LIBRARY_PATH"

LAUNCHER_BIN="$SCRIPT_DIR/tools/launcher/src-tauri/target/release/launcher"

if [ -f "$LAUNCHER_BIN" ]; then
    echo "==> Starting UltraTears Launcher..."
    exec "$LAUNCHER_BIN" "$@"
else
    echo "==> UltraTears Launcher binary not found. Building release binary..."
    cd tools/launcher
    npm run build
    cargo build --release --manifest-path src-tauri/Cargo.toml
    cd "$SCRIPT_DIR"
    exec "$LAUNCHER_BIN" "$@"
fi
