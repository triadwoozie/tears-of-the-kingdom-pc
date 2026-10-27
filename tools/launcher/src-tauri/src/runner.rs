use crate::config::LauncherConfig;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::PathBuf;
use std::sync::Arc;
use std::time::{SystemTime, UNIX_EPOCH};
use tauri::{AppHandle, Emitter};
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::process::{Child, Command};
use tokio::sync::Mutex;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EngineState {
    pub is_running: bool,
    pub pid: Option<u32>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EngineLogEntry {
    pub channel: String, // "gfx", "thread", "error", "info"
    pub text: String,
    pub timestamp: u64,
}

pub struct RunnerState {
    pub child: Arc<Mutex<Option<Child>>>,
}

impl RunnerState {
    pub fn new() -> Self {
        Self {
            child: Arc::new(Mutex::new(None)),
        }
    }
}

fn find_engine_binary(custom_path: &str) -> Result<PathBuf, String> {
    if !custom_path.trim().is_empty() {
        let p = PathBuf::from(custom_path);
        if p.exists() {
            return Ok(p);
        }
    }

    #[cfg(target_os = "windows")]
    let candidates = [
        "ultratears.exe",
        "./build/ultratears.exe",
        "./refs/ultratears/build/ultratears.exe",
        "../build/ultratears.exe",
        "../../build/ultratears.exe",
    ];

    #[cfg(not(target_os = "windows"))]
    let candidates = [
        "ultratears",
        "./build/ultratears",
        "./refs/ultratears/build/ultratears",
        "../build/ultratears",
        "../../build/ultratears",
        "/home/ryan/totk-desktop/refs/ultratears/build/ultratears",
        "/home/ryan/totk-desktop/build/ultratears",
    ];

    for c in &candidates {
        let p = PathBuf::from(c);
        if p.exists() {
            return Ok(p);
        }
    }

    Err("Engine binary (ultratears) not found. Build the project first or specify custom path.".to_string())
}

#[tauri::command]
pub async fn get_engine_status(
    state: tauri::State<'_, RunnerState>,
) -> Result<EngineState, String> {
    let mut guard = state.child.lock().await;
    if let Some(child) = guard.as_mut() {
        match child.try_wait() {
            Ok(Some(_status)) => {
                *guard = None;
                Ok(EngineState {
                    is_running: false,
                    pid: None,
                })
            }
            Ok(None) => Ok(EngineState {
                is_running: true,
                pid: child.id(),
            }),
            Err(_) => {
                *guard = None;
                Ok(EngineState {
                    is_running: false,
                    pid: None,
                })
            }
        }
    } else {
        Ok(EngineState {
            is_running: false,
            pid: None,
        })
    }
}

#[tauri::command]
pub async fn stop_engine(
    app: AppHandle,
    state: tauri::State<'_, RunnerState>,
) -> Result<(), String> {
    let mut guard = state.child.lock().await;
    if let Some(mut child) = guard.take() {
        let _ = child.kill().await;
        let _ = app.emit("engine-status", EngineState {
            is_running: false,
            pid: None,
        });
        let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_millis() as u64;
        let _ = app.emit("engine-log", EngineLogEntry {
            channel: "info".to_string(),
            text: "--- Engine process terminated by user ---".to_string(),
            timestamp: now,
        });
    }
    Ok(())
}

#[tauri::command]
pub async fn launch_engine(
    app: AppHandle,
    state: tauri::State<'_, RunnerState>,
    config: LauncherConfig,
) -> Result<u32, String> {
    let mut guard = state.child.lock().await;
    if guard.is_some() {
        return Err("Engine is already running".to_string());
    }

    let bin_path = find_engine_binary(&config.engine_binary_path)?;

    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        if let Ok(meta) = fs::metadata(&bin_path) {
            let mut perms = meta.permissions();
            perms.set_mode(0o755);
            let _ = fs::set_permissions(&bin_path, perms);
        }
    }

    let mut cmd = Command::new(&bin_path);
    if let Some(parent) = bin_path.parent() {
        if parent.as_os_str().len() > 0 {
            cmd.current_dir(parent);
        }
    }

    // Set Environment Variables
    if !config.romfs_path.is_empty() {
        cmd.env("TOTK_ROMFS", &config.romfs_path);
    }
    cmd.env("TOTK_HUD", if config.graphics.enable_hud { "1" } else { "0" });
    cmd.env("TOTK_HEADLESS", if config.graphics.headless { "1" } else { "0" });
    cmd.env("TOTK_DOCK", if config.display.docked_mode { "1" } else { "0" });
    cmd.env("TOTK_VSYNC", &config.display.vsync_mode);
    cmd.env("TOTK_ASPECT", &config.display.aspect_ratio);
    cmd.env("TOTK_ASYNC_REPLAY", if config.graphics.async_replay { "1" } else { "0" });
    cmd.env("TOTK_VK_DEVICE_INDEX", config.graphics.vulkan_device_index.to_string());

    let res_scale = match config.display.resolution.as_str() {
        "720p" => "0.666",
        "1080p" => "1.0",
        "1440p" => "1.333",
        "4K" => "2.0",
        _ => "1.0",
    };
    cmd.env("TOTK_RES_SCALE", res_scale);

    if !config.controller.active_guid.is_empty() {
        cmd.env("TOTK_PAD_GUID", &config.controller.active_guid);
    }
    cmd.env("TOTK_PAD_SWAP_AB_XY", if config.controller.swap_ab_xy { "1" } else { "0" });
    cmd.env("TOTK_PAD_DEADZONE_L", config.controller.left_deadzone.to_string());
    cmd.env("TOTK_PAD_DEADZONE_R", config.controller.right_deadzone.to_string());
    cmd.env("TOTK_PAD_TRIGGER_THRESH", config.controller.trigger_threshold.to_string());
    cmd.env("TOTK_PAD_GYRO", if config.controller.gyro_enabled { "1" } else { "0" });
    cmd.env("TOTK_PAD_GYRO_SENS", config.controller.gyro_sensitivity.to_string());
    cmd.env("TOTK_PAD_RUMBLE", if config.controller.rumble_enabled { "1" } else { "0" });
    cmd.env("TOTK_PAD_RUMBLE_INTENSITY", config.controller.rumble_intensity.to_string());
    cmd.env("TOTK_PAD_HD_RUMBLE", if config.controller.hd_rumble_lra_sim { "1" } else { "0" });

    // Patches
    for patch in &config.patches {
        if patch.enabled {
            match patch.id.as_str() {
                "patch_zero_heap" => { cmd.env("TOTK_PATCH_ZERO_HEAP_FALLBACK", "1"); },
                "patch_loose_romfs" => { cmd.env("TOTK_PATCH_LOOSE_ROMFS", "1"); },
                "patch_60fps_lock" => { cmd.env("TOTK_PATCH_60FPS", "1"); },
                "patch_disable_drs" => { cmd.env("TOTK_PATCH_DISABLE_DRS", "1"); },
                "patch_ultrawide" => { cmd.env("TOTK_PATCH_ULTRAWIDE", "1"); },
                _ => {},
            }
        }
    }

    for (k, v) in &config.custom_env {
        cmd.env(k, v);
    }

    cmd.stdout(std::process::Stdio::piped());
    cmd.stderr(std::process::Stdio::piped());

    let mut child = cmd.spawn().map_err(|e| format!("Failed to spawn {}: {}", bin_path.display(), e))?;
    let pid = child.id().unwrap_or(0);

    let stdout = child.stdout.take();
    let stderr = child.stderr.take();

    let app_clone1 = app.clone();
    if let Some(out) = stdout {
        tokio::spawn(async move {
            let mut reader = BufReader::new(out).lines();
            while let Ok(Some(line)) = reader.next_line().await {
                let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_millis() as u64;
                let channel = if line.contains("[totk-gfx]") || line.contains("[vulkan]") || line.contains("NVN") {
                    "gfx"
                } else if line.contains("[totk-thread]") || line.contains("svc") || line.contains("thread") {
                    "thread"
                } else if line.contains("SIGSEGV") || line.contains("fault") || line.contains("error") || line.contains("fatal") || line.contains("***") {
                    "error"
                } else {
                    "info"
                };
                let _ = app_clone1.emit("engine-log", EngineLogEntry {
                    channel: channel.to_string(),
                    text: line,
                    timestamp: now,
                });
            }
        });
    }

    let app_clone2 = app.clone();
    if let Some(err) = stderr {
        tokio::spawn(async move {
            let mut reader = BufReader::new(err).lines();
            while let Ok(Some(line)) = reader.next_line().await {
                let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_millis() as u64;
                let channel = if line.contains("[totk-gfx]") {
                    "gfx"
                } else if line.contains("SIGSEGV") || line.contains("fault") || line.contains("error") || line.contains("fatal") || line.contains("***") {
                    "error"
                } else {
                    "error"
                };
                let _ = app_clone2.emit("engine-log", EngineLogEntry {
                    channel: channel.to_string(),
                    text: line,
                    timestamp: now,
                });
            }
        });
    }

    let _ = app.emit("engine-status", EngineState {
        is_running: true,
        pid: Some(pid),
    });

    *guard = Some(child);
    Ok(pid)
}
