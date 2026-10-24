#pragma once

#include <cstdint>
#include <string>

namespace totk::ui {

struct RomState {
    char base_path[512] = "";
    char update_path[512] = "";
    char keys_path[512] = "";

    bool base_valid = false;
    bool update_valid = false;
    bool keys_valid = false;

    std::string base_hash;
    std::string update_hash;
    int keys_count = 0;

    bool is_recompiling = false;
    float recomp_progress = 0.0f;
    std::string recomp_stage_name = "Idle";
};

struct ControllerSettings {
    int active_controller_index = 0;
    int prompt_theme = 0; // 0 = Nintendo, 1 = Xbox, 2 = PlayStation
    float left_deadzone = 0.10f;
    float right_deadzone = 0.10f;
    bool enable_gyro = true;
    float gyro_sensitivity = 1.0f;
    bool enable_rumble = true;
    float rumble_intensity = 0.8f;
};

struct GraphicsSettings {
    int resolution_scale_idx = 1; // 0=Native(900p), 1=1080p, 2=1440p, 3=4K
    int aspect_ratio_idx = 0;     // 0=16:9, 1=16:10, 2=21:9, 3=32:9
    int fps_cap_idx = 1;          // 0=30, 1=60, 2=120, 3=Unlocked
    bool vsync = true;
    bool enable_fsr = true;
    float fsr_sharpness = 0.8f;

    uint32_t cached_shaders = 2418;
    uint32_t compiled_pipelines = 4120;
};

class Studio {
public:
    Studio();
    ~Studio();

    void render(bool *p_open, bool *p_launch_requested);

    RomState &get_rom_state() { return rom_state_; }
    ControllerSettings &get_controller_settings() { return controller_settings_; }
    GraphicsSettings &get_graphics_settings() { return graphics_settings_; }

private:
    void render_rom_keys_tab();
    void render_controller_studio_tab();
    void render_graphics_tuner_tab();
    void render_audio_tab();

    RomState rom_state_;
    ControllerSettings controller_settings_;
    GraphicsSettings graphics_settings_;
};

} // namespace totk::ui
