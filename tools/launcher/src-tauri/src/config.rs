use directories::ProjectDirs;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs;
use std::path::PathBuf;

fn default_framerate_mode() -> String {
    "60".to_string()
}

fn default_custom_hz() -> u32 {
    144
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DisplayConfig {
    pub resolution: String,
    pub aspect_ratio: String,
    pub vsync_mode: String,
    pub docked_mode: bool,
    pub fullscreen: bool,
    #[serde(default = "default_framerate_mode")]
    pub framerate_mode: String,
    #[serde(default = "default_custom_hz")]
    pub target_hz: u32,
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

fn default_true() -> bool {
    true
}

fn default_sens() -> f32 {
    1.0
}

fn default_keybinds() -> HashMap<String, String> {
    let mut m = HashMap::new();
    m.insert("MOVE_UP".to_string(), "26,0".to_string());    // W
    m.insert("MOVE_DOWN".to_string(), "22,0".to_string());  // S
    m.insert("MOVE_LEFT".to_string(), "4,0".to_string());   // A
    m.insert("MOVE_RIGHT".to_string(), "7,0".to_string());  // D
    m.insert("X".to_string(), "44,0".to_string());          // Space (Jump)
    m.insert("B".to_string(), "225,41".to_string());        // Shift / Esc (Sprint)
    m.insert("Y".to_string(), "512,9".to_string());         // LMB (Attack)
    m.insert("ZR".to_string(), "513,21".to_string());       // RMB (Bow)
    m.insert("ZL".to_string(), "514,20".to_string());       // MMB (Lock on)
    m.insert("A".to_string(), "8,0".to_string());           // E (Interact)
    m.insert("L".to_string(), "6,0".to_string());           // C (Ability)
    m.insert("R".to_string(), "23,0".to_string());          // T (Throw)
    m.insert("PLUS".to_string(), "43,0".to_string());       // Tab (Pouch)
    m.insert("MINUS".to_string(), "53,16".to_string());     // Grave (Purah Pad)
    m
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MouseConfig {
    #[serde(default = "default_true")]
    pub mouse_cam_enabled: bool,
    #[serde(default = "default_sens")]
    pub mouse_sens_x: f32,
    #[serde(default = "default_sens")]
    pub mouse_sens_y: f32,
    #[serde(default)]
    pub invert_y: bool,
    #[serde(default)]
    pub invert_x: bool,
    #[serde(default = "default_true")]
    pub lock_cursor: bool,
    #[serde(default = "default_keybinds")]
    pub keybinds: HashMap<String, String>,
}

impl Default for MouseConfig {
    fn default() -> Self {
        Self {
            mouse_cam_enabled: true,
            mouse_sens_x: 1.0,
            mouse_sens_y: 1.0,
            invert_y: false,
            invert_x: false,
            lock_cursor: true,
            keybinds: default_keybinds(),
        }
    }
}

fn default_shaders_pack_path() -> String {
    "data/shaders/ryujinx/shaders.pack".to_string()
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ShaderPackConfig {
    #[serde(default = "default_true")]
    pub use_shaders_pack: bool,
    #[serde(default = "default_shaders_pack_path")]
    pub shaders_pack_path: String,
}

impl Default for ShaderPackConfig {
    fn default() -> Self {
        Self {
            use_shaders_pack: true,
            shaders_pack_path: default_shaders_pack_path(),
        }
    }
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
    #[serde(default)]
    pub mouse: MouseConfig,
    #[serde(default)]
    pub shader_pack: ShaderPackConfig,
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
            framerate_mode: "60".to_string(),
            target_hz: 144,
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
        mouse: MouseConfig::default(),
        shader_pack: ShaderPackConfig::default(),
        patches: vec![
            PatchConfig {
                id: "patch_zero_heap".to_string(),
                name: "sead::HeapMgr Zero-Heap Arena Fallback".to_string(),
                enabled: true,
                description: "Intercepts direct-call null allocations at startup using a 32 MiB thread-safe fallback arena.".to_string(),
            },
            PatchConfig {
                id: "patch_jump_table_fallback".to_string(),
                name: "Lifter Jump-Table Target Fallback".to_string(),
                enabled: true,
                description: "Applies dynamic target fallback for unconfirmed branch tables in save/load routines.".to_string(),
            },
            PatchConfig {
                id: "patch_shaders_pack".to_string(),
                name: "Packed Shader Archive Loader".to_string(),
                enabled: true,
                description: "Loads optimized single-file binary shader cache (shaders.pack) instead of loose disk files.".to_string(),
            },
            PatchConfig {
                id: "patch_mouse_camera".to_string(),
                name: "Native Mouse Camera Panning".to_string(),
                enabled: true,
                description: "Enables raw sub-pixel mouse look and camera state hooks with F8 cursor toggle.".to_string(),
            },
            PatchConfig {
                id: "patch_loose_romfs".to_string(),
                name: "Loose RomFS Asset Override".to_string(),
                enabled: true,
                description: "Allows loading individual game assets directly from unpacked RomFS folder.".to_string(),
            },
            PatchConfig {
                id: "patch_60fps_lock".to_string(),
                name: "60 FPS Dynamic Timestep".to_string(),
                enabled: true,
                description: "Uncaps standard 30 FPS console lock and synchronizes game physics and simulation to 60 FPS.".to_string(),
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
