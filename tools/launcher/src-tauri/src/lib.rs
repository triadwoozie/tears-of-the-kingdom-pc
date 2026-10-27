mod assets;
mod config;
mod controller;
mod runner;

use controller::ControllerState;
use runner::RunnerState;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_dialog::init())
        .manage(ControllerState::new())
        .manage(RunnerState::new())
        .invoke_handler(tauri::generate_handler![
            config::get_config,
            config::save_config,
            controller::list_controllers,
            controller::poll_controller_preview,
            controller::test_rumble,
            assets::validate_romfs,
            assets::pack_romfs,
            assets::pack_shaders,
            runner::launch_engine,
            runner::stop_engine,
            runner::get_engine_status,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
