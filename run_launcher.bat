@echo off
setlocal

REM UltraTears Desktop Launcher (Windows Runner)
set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

set "LAUNCHER_BIN=%SCRIPT_DIR%tools\launcher\src-tauri\target\release\launcher.exe"

if exist "%LAUNCHER_BIN%" (
    echo ==> Starting UltraTears Launcher...
    start "" "%LAUNCHER_BIN%" %*
) else (
    echo ==> UltraTears Launcher binary not found. Building release binary...
    cd tools\launcher
    call npm run build
    cargo build --release --manifest-path src-tauri\Cargo.toml
    cd /d "%SCRIPT_DIR%"
    start "" "%LAUNCHER_BIN%" %*
)

endlocal
