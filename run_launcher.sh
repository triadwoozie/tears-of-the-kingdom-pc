#!/usr/bin/env bash
set -e

# UltraTears Desktop Launcher (Linux Runner)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

export PATH="$HOME/.cargo/bin:$HOME/.local/bin:$PATH"
export PKG_CONFIG_PATH="$HOME/.local/usr/lib64/pkgconfig:$HOME/.local/usr/lib/pkgconfig:$HOME/.local/usr/share/pkgconfig:$PKG_CONFIG_PATH"
export LD_LIBRARY_PATH="$HOME/.local/usr/lib64:$HOME/.local/usr/lib:$LD_LIBRARY_PATH"

STANDALONE_BIN="$SCRIPT_DIR/bin/linux-x64/totk-launcher"
TARGET_BIN="$SCRIPT_DIR/tools/launcher/src-tauri/target/release/launcher"

# 1. First: Check if standalone prebuilt linux-x64 binary exists
if [ -x "$STANDALONE_BIN" ]; then
    echo "==> Starting UltraTears Launcher (Standalone Binary: $STANDALONE_BIN)..."
    if ! "$STANDALONE_BIN" "$@" 2>/dev/null; then
        echo "==> Launcher execution exited. Checking runtime shared libraries via ldd..."
        MISSING_LIBS=$(ldd "$STANDALONE_BIN" 2>&1 | grep "not found" || true)
        if [ -n "$MISSING_LIBS" ]; then
            echo "==> Missing dynamic system libraries detected:"
            echo "$MISSING_LIBS"
            echo "==> Hint: Ensure webkit2gtk-4.1, gtk3, and libsoup3 runtime packages are installed."
        fi
    fi
    exit 0
fi

# 2. Second: Check if target release binary exists
if [ -x "$TARGET_BIN" ]; then
    echo "==> Starting UltraTears Launcher ($TARGET_BIN)..."
    exec "$TARGET_BIN" "$@"
fi

# 3. Third: If cargo and npm are installed, build or run release binary
if command -v cargo >/dev/null 2>&1 && command -v npm >/dev/null 2>&1; then
    echo "==> Building UltraTears Launcher release binary via Cargo & Vite..."
    cd tools/launcher
    npm run build
    cargo build --release --manifest-path src-tauri/Cargo.toml
    mkdir -p "$SCRIPT_DIR/bin/linux-x64"
    cp src-tauri/target/release/launcher "$STANDALONE_BIN"
    chmod +x "$STANDALONE_BIN"
    cd "$SCRIPT_DIR"
    exec "$STANDALONE_BIN" "$@"
fi

# 4. Fallback: Start Vite UI server + local headless info bridge
echo "==> Native Tauri binary not found and cargo not installed."
echo "==> Falling back to local Vite preview server..."
if [ -d "$SCRIPT_DIR/tools/launcher/dist" ]; then
    echo "==> Serving tools/launcher/dist at http://127.0.0.1:1420..."
    python3 -m http.server 1420 --directory "$SCRIPT_DIR/tools/launcher/dist"
elif command -v npm >/dev/null 2>&1; then
    cd "$SCRIPT_DIR/tools/launcher"
    npm run dev -- --host 127.0.0.1 --port 1420
else
    echo "==> Error: Neither prebuilt binary nor build toolchain available."
    exit 1
fi
