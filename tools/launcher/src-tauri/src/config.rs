use directories::ProjectDirs;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs;
use std::path::PathBuf;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DisplayConfig {
    pub resolution: String,
    pub aspect_ratio: String,
    pub vsync_mode: String,
    pub docked_mode: bool,
    pub fullscreen: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GraphicsConfig {
    pub enable_hud: bool,
    pub async_replay: bool,
    pub headless: bool,
    pub vulkan_device_index: usize,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ControllerConfig {
    pub active_guid: String,
    pub profile: String,
    pub left_deadzone: f32,
    pub right_deadzone: f32,
    pub trigger_threshold: f32,
    pub gyro_enabled: bool,
    pub gyro_sensitivity: f32,
    pub swap_ab_xy: bool,
    pub rumble_enabled: bool,
    pub rumble_intensity: f32,
    pub hd_rumble_lra_sim: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PatchConfig {
    pub id: String,
    pub name: String,
    pub enabled: bool,
    pub description: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LauncherConfig {
    pub theme: String,
    pub romfs_path: String,
    pub engine_binary_path: String,
    pub display: DisplayConfig,
    pub graphics: GraphicsConfig,
    pub controller: ControllerConfig,
    pub patches: Vec<PatchConfig>,
    pub custom_env: HashMap<String, String>,
}

fn config_path() -> Result<PathBuf, String> {
    let proj = ProjectDirs::from("com", "ultratears", "launcher")
        .ok_or_else(|| "Failed to determine user home/config directory".to_string())?;
    let config_dir = proj.config_dir();
    fs::create_dir_all(config_dir)
        .map_err(|e| format!("Failed to create config dir {}: {}", config_dir.display(), e))?;
    Ok(config_dir.join("launcher.json"))
}

pub fn get_default_config() -> LauncherConfig {
    LauncherConfig {
        theme: "sky-island-dark".to_string(),
        romfs_path: "".to_string(),
        engine_binary_path: "".to_string(),
        display: DisplayConfig {
            resolution: "1080p".to_string(),
            aspect_ratio: "16:9".to_string(),
            vsync_mode: "Mailbox".to_string(),
            docked_mode: true,
            fullscreen: false,
        },
        graphics: GraphicsConfig {
            enable_hud: true,
            async_replay: false,
            headless: false,
            vulkan_device_index: 0,
        },
        controller: ControllerConfig {
            active_guid: "".to_string(),
            profile: "Nintendo (B/A, Y/X)".to_string(),
            left_deadzone: 0.10,
            right_deadzone: 0.10,
            trigger_threshold: 0.05,
            gyro_enabled: true,
            gyro_sensitivity: 1.0,
            swap_ab_xy: false,
            rumble_enabled: true,
            rumble_intensity: 1.0,
            hd_rumble_lra_sim: true,
        },
        patches: vec![
            PatchConfig {
                id: "patch_zero_heap".to_string(),
                name: "sead::HeapMgr Zero-Heap Fallback Arena (Startup Fix)".to_string(),
                enabled: true,
                description: "Intercepts direct-call null allocations at startup using a 32 MiB thread-safe fallback arena.".to_string(),
            },
            PatchConfig {
                id: "patch_loose_romfs".to_string(),
                name: "Loose RomFS Directory Override".to_string(),
                enabled: true,
                description: "Allows loading individual game assets directly from unpacked RomFS folder.".to_string(),
            },
            PatchConfig {
                id: "patch_60fps_lock".to_string(),
                name: "Dynamic FPS / 60 FPS Lock".to_string(),
                enabled: false,
                description: "Uncaps standard 30 FPS console lock and synchronizes game speed to 60 FPS.".to_string(),
            },
            PatchConfig {
                id: "patch_disable_drs".to_string(),
                name: "Disable Dynamic Resolution Scaling (DRS)".to_string(),
                enabled: false,
                description: "Locks internal render resolution to target display scale without dynamic drop.".to_string(),
            },
            PatchConfig {
                id: "patch_ultrawide".to_string(),
                name: "Ultrawide UI Aspect Fix".to_string(),
                enabled: false,
                description: "Adjusts viewport projection and 2D UI elements for 21:9 / 32:9 displays.".to_string(),
            },
        ],
        custom_env: HashMap::new(),
    }
}

#[tauri::command]
pub fn get_config() -> Result<LauncherConfig, String> {
    let path = config_path()?;
    if !path.exists() {
        let default_cfg = get_default_config();
        let _ = save_config(default_cfg.clone());
        return Ok(default_cfg);
    }

    let contents = fs::read_to_string(&path)
        .map_err(|e| format!("Failed to read {}: {}", path.display(), e))?;
    let config: LauncherConfig = serde_json::from_str(&contents)
        .unwrap_or_else(|_| get_default_config());
    Ok(config)
}

#[tauri::command]
pub fn save_config(config: LauncherConfig) -> Result<(), String> {
    let path = config_path()?;
    let json = serde_json::to_string_pretty(&config)
        .map_err(|e| format!("Failed to serialize config: {}", e))?;
    fs::write(&path, json)
        .map_err(|e| format!("Failed to write {}: {}", path.display(), e))?;
    Ok(())
}
