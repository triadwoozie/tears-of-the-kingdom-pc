use gilrs::ff::{BaseEffect, BaseEffectType, EffectBuilder, Replay, Ticks};
use gilrs::{Axis, Gilrs};
use serde::{Deserialize, Serialize};
use std::sync::Mutex;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GamepadInfo {
    pub id: String,
    pub name: String,
    pub is_connected: bool,
    pub has_rumble: bool,
    pub power_info: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StickPreview {
    pub lx: f32,
    pub ly: f32,
    pub rx: f32,
    pub ry: f32,
    pub l2: f32,
    pub r2: f32,
}

pub struct ControllerState {
    pub gilrs: Mutex<Gilrs>,
}

impl ControllerState {
    pub fn new() -> Self {
        let gilrs = Gilrs::new().unwrap_or_else(|_| {
            eprintln!("Warning: Failed to initialize gilrs subsystem");
            Gilrs::new().unwrap()
        });
        Self {
            gilrs: Mutex::new(gilrs),
        }
    }
}

#[tauri::command]
pub fn list_controllers(
    state: tauri::State<'_, ControllerState>,
) -> Result<Vec<GamepadInfo>, String> {
    let mut gilrs = state
        .gilrs
        .lock()
        .map_err(|e| format!("Gilrs lock error: {}", e))?;

    // Drain events to keep device list up to date
    while let Some(_) = gilrs.next_event() {}

    let mut controllers = Vec::new();
    for (id, gamepad) in gilrs.gamepads() {
        let power_str = match gamepad.power_info() {
            gilrs::PowerInfo::Unknown => "Unknown".to_string(),
            gilrs::PowerInfo::Wired => "Wired (USB)".to_string(),
            gilrs::PowerInfo::Discharging(lvl) => format!("Battery {}%", lvl),
            gilrs::PowerInfo::Charging(lvl) => format!("Charging ({}%)", lvl),
            gilrs::PowerInfo::Charged => "Battery 100%".to_string(),
        };

        controllers.push(GamepadInfo {
            id: usize::from(id).to_string(),
            name: gamepad.name().to_string(),
            is_connected: gamepad.is_connected(),
            has_rumble: gamepad.is_ff_supported(),
            power_info: power_str,
        });
    }

    Ok(controllers)
}

#[tauri::command]
pub fn poll_controller_preview(
    state: tauri::State<'_, ControllerState>,
    id: String,
) -> Result<StickPreview, String> {
    let mut gilrs = state
        .gilrs
        .lock()
        .map_err(|e| format!("Gilrs lock error: {}", e))?;

    while let Some(_) = gilrs.next_event() {}

    let target_id = gilrs
        .gamepads()
        .find(|(gid, _)| usize::from(*gid).to_string() == id)
        .map(|(gid, _)| gid);

    match target_id {
        Some(gid) => {
            let pad = gilrs.gamepad(gid);
            let lx = pad.axis_data(Axis::LeftStickX).map(|a| a.value()).unwrap_or(0.0);
            let ly = pad.axis_data(Axis::LeftStickY).map(|a| a.value()).unwrap_or(0.0);
            let rx = pad.axis_data(Axis::RightStickX).map(|a| a.value()).unwrap_or(0.0);
            let ry = pad.axis_data(Axis::RightStickY).map(|a| a.value()).unwrap_or(0.0);
            let l2 = pad.axis_data(Axis::LeftZ).map(|a| a.value()).unwrap_or(0.0);
            let r2 = pad.axis_data(Axis::RightZ).map(|a| a.value()).unwrap_or(0.0);

            Ok(StickPreview {
                lx,
                ly,
                rx,
                ry,
                l2: if l2 < 0.0 { 0.0 } else { l2 },
                r2: if r2 < 0.0 { 0.0 } else { r2 },
            })
        }
        None => Ok(StickPreview {
            lx: 0.0,
            ly: 0.0,
            rx: 0.0,
            ry: 0.0,
            l2: 0.0,
            r2: 0.0,
        }),
    }
}

#[tauri::command]
pub fn test_rumble(
    state: tauri::State<'_, ControllerState>,
    id: String,
    low_freq: f32,
    high_freq: f32,
    duration_ms: u64,
) -> Result<(), String> {
    let mut gilrs = state
        .gilrs
        .lock()
        .map_err(|e| format!("Gilrs lock error: {}", e))?;

    let target_id = gilrs
        .gamepads()
        .find(|(gid, _)| usize::from(*gid).to_string() == id)
        .map(|(gid, _)| gid);

    let gid = match target_id {
        Some(id) => id,
        None => return Ok(()),
    };

    let duration = Ticks::from_ms((duration_ms.clamp(50, 2000)) as u32);
    let low_mag = ((low_freq.clamp(0.0, 1.5) / 1.5) * 65535.0) as u16;
    let high_mag = ((high_freq.clamp(0.0, 1.5) / 1.5) * 65535.0) as u16;

    let effect = EffectBuilder::new()
        .add_effect(BaseEffect {
            kind: BaseEffectType::Strong {
                magnitude: low_mag,
            },
            scheduling: Replay {
                play_for: duration,
                ..Default::default()
            },
            envelope: Default::default(),
        })
        .add_effect(BaseEffect {
            kind: BaseEffectType::Weak {
                magnitude: high_mag,
            },
            scheduling: Replay {
                play_for: duration,
                ..Default::default()
            },
            envelope: Default::default(),
        })
        .gamepads(&[gid])
        .finish(&mut gilrs)
        .map_err(|e| format!("Failed to create rumble effect: {:?}", e))?;

    let _ = effect.play();
    Ok(())
}
