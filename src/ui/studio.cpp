#include "studio.h"
#include "imgui.h"
#include <cstdio>
#include <cstring>

namespace totk::ui {

Studio::Studio() {
    // Default mock paths if present in local/
    std::strncpy(rom_state_.base_path, "local/totk_base.nsp", sizeof(rom_state_.base_path));
    std::strncpy(rom_state_.update_path, "local/totk_v1.4.3.nsp", sizeof(rom_state_.update_path));
    std::strncpy(rom_state_.keys_path, "local/prod.keys", sizeof(rom_state_.keys_path));
}

Studio::~Studio() = default;

void Studio::render(bool *p_open, bool *p_launch_requested) {
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(880, 600), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    if (!ImGui::Begin("TotK-Recomp Studio", p_open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("StudioTabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("ROM & Keys")) {
            render_rom_keys_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Controller Studio")) {
            render_controller_studio_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Graphics Tuner")) {
            render_graphics_tuner_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Audio & System")) {
            render_audio_tab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Footer actions
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.65f, 1.0f), "Tears of the Kingdom v1.4.3 Recompilation Environment");
    ImGui::SameLine(avail - 280.0f);

    if (ImGui::Button("Run Recompile", ImVec2(130, 36))) {
        rom_state_.is_recompiling = true;
        rom_state_.recomp_progress = 0.05f;
        rom_state_.recomp_stage_name = "Extracting ExeFS modules...";
    }

    ImGui::SameLine();
    bool can_launch = !rom_state_.is_recompiling;
    if (!can_launch) ImGui::BeginDisabled();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.55f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.68f, 0.38f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.45f, 0.25f, 1.0f));

    if (ImGui::Button("Launch Game ▶", ImVec2(140, 36))) {
        if (p_launch_requested) *p_launch_requested = true;
    }

    ImGui::PopStyleColor(3);
    if (!can_launch) ImGui::EndDisabled();

    ImGui::End();
}

void Studio::render_rom_keys_tab() {
    ImGui::Spacing();
    ImGui::TextWrapped("Select your legally acquired Tears of the Kingdom v1.4.3 game package files and encryption keys.");
    ImGui::Spacing();

    // Base ROM Input
    ImGui::Text("Base Game Package (.nsp / .xci / folder):");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 120.0f);
    ImGui::InputText("##BasePath", rom_state_.base_path, sizeof(rom_state_.base_path));
    ImGui::SameLine();
    if (ImGui::Button("Browse...##Base", ImVec2(110, 0))) {
        // Native browse hooked up in platform layer
    }

    // Update File Input
    ImGui::Spacing();
    ImGui::Text("Update Package (v1.4.3 required):");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 120.0f);
    ImGui::InputText("##UpdatePath", rom_state_.update_path, sizeof(rom_state_.update_path));
    ImGui::SameLine();
    if (ImGui::Button("Browse...##Update", ImVec2(110, 0))) {
    }

    // Keys Input
    ImGui::Spacing();
    ImGui::Text("Switch Decryption Keys (prod.keys):");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 120.0f);
    ImGui::InputText("##KeysPath", rom_state_.keys_path, sizeof(rom_state_.keys_path));
    ImGui::SameLine();
    if (ImGui::Button("Browse...##Keys", ImVec2(110, 0))) {
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Verification Badges
    ImGui::Text("Verification & Integrity Status:");
    ImGui::Bullet();
    ImGui::Text("v1.4.3 Main Hash: ");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.5f, 1.0f), "e84a90d551fed0824b4b74933b9c6d043c458521363a7cea58b2faca1822dc9e");

    ImGui::Bullet();
    ImGui::Text("v1.4.3 SubSDK0 Hash: ");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.5f, 1.0f), "767c35bcd7d23e78f9063c69b638290b8b83f91e839beb10799e6576960179b1");

    ImGui::Bullet();
    ImGui::Text("v1.4.3 SDK Hash: ");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.5f, 1.0f), "1d187f29784cd0af46e9e123fc2a62c77b3b07c5a39c8633d90d6c80de64e4ad");

    ImGui::Spacing();
    ImGui::Text("Recompile Progress: %s", rom_state_.recomp_stage_name.c_str());
    ImGui::ProgressBar(rom_state_.recomp_progress, ImVec2(-1.0f, 22.0f));
}

void Studio::render_controller_studio_tab() {
    ImGui::Spacing();
    ImGui::Text("Controller Configuration & Motion Controls (SDL3 Gamepad API)");
    ImGui::Spacing();

    const char *controllers[] = { "Controller 1: DualSense Wireless Controller", "Controller 2: Nintendo Switch Pro Controller", "Keyboard & Mouse Emulation" };
    ImGui::Combo("Active Device", &controller_settings_.active_controller_index, controllers, IM_ARRAYSIZE(controllers));

    const char *prompt_themes[] = { "Nintendo Switch (A/B, X/Y)", "Xbox (A/B, X/Y)", "PlayStation (✕/○, □/△)" };
    ImGui::Combo("Button Prompts", &controller_settings_.prompt_theme, prompt_themes, IM_ARRAYSIZE(prompt_themes));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Stick Deadzone Calibration:");
    ImGui::SliderFloat("Left Stick Deadzone", &controller_settings_.left_deadzone, 0.0f, 0.35f, "%.2f");
    ImGui::SliderFloat("Right Stick Deadzone", &controller_settings_.right_deadzone, 0.0f, 0.35f, "%.2f");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Checkbox("Enable Motion Controls / Gyro Aiming (SDL3 Sensor API)", &controller_settings_.enable_gyro);
    if (controller_settings_.enable_gyro) {
        ImGui::SliderFloat("Gyro Sensitivity", &controller_settings_.gyro_sensitivity, 0.1f, 3.0f, "%.2fx");
    }

    ImGui::Spacing();
    ImGui::Checkbox("Enable HD Rumble Emulation", &controller_settings_.enable_rumble);
    if (controller_settings_.enable_rumble) {
        ImGui::SliderFloat("Rumble Force", &controller_settings_.rumble_intensity, 0.0f, 1.0f, "%.0f%%");
    }
}

void Studio::render_graphics_tuner_tab() {
    ImGui::Spacing();
    ImGui::Text("Vulkan 1.3 Graphics Engine & Aspect Ratio Configuration");
    ImGui::Spacing();

    const char *resolutions[] = { "Native Docked (900p)", "1080p FHD (1.2x)", "1440p QHD (1.77x)", "2160p 4K UHD (2.66x)" };
    ImGui::Combo("Render Resolution", &graphics_settings_.resolution_scale_idx, resolutions, IM_ARRAYSIZE(resolutions));

    const char *aspects[] = { "16:9 Standard", "16:10 (Steam Deck / Handhelds)", "21:9 Ultrawide (Dynamic FOV Patch)", "32:9 Super Ultrawide" };
    ImGui::Combo("Aspect Ratio", &graphics_settings_.aspect_ratio_idx, aspects, IM_ARRAYSIZE(aspects));

    const char *fps_caps[] = { "30 FPS (Console Standard)", "60 FPS (Game Timing Patch)", "120 FPS High Refresh", "Unlocked Framerate" };
    ImGui::Combo("Framerate Cap", &graphics_settings_.fps_cap_idx, fps_caps, IM_ARRAYSIZE(fps_caps));

    ImGui::Checkbox("Vertical Sync (VSync / Mailbox)", &graphics_settings_.vsync);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Checkbox("Enable AMD FidelityFX Super Resolution (FSR)", &graphics_settings_.enable_fsr);
    if (graphics_settings_.enable_fsr) {
        ImGui::SliderFloat("FSR RCAS Sharpness", &graphics_settings_.fsr_sharpness, 0.0f, 1.0f, "%.2f");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Shader & Pipeline Cache Status:");
    ImGui::BulletText("Validated SPIR-V Modules: %u", graphics_settings_.cached_shaders);
    ImGui::BulletText("Compiled Vulkan Pipelines: %u", graphics_settings_.compiled_pipelines);

    if (ImGui::Button("Pre-Warm Pipeline Cache")) {
        // Triggers offline warm up
    }
}

void Studio::render_audio_tab() {
    ImGui::Spacing();
    ImGui::Text("SDL3 Audio Stream & Horizon Voice Renderer");
    ImGui::Spacing();

    static float master_volume = 1.0f;
    static float music_volume = 0.8f;
    static float sfx_volume = 1.0f;
    static float voice_volume = 1.0f;

    ImGui::SliderFloat("Master Volume", &master_volume, 0.0f, 1.0f, "%.0f%%");
    ImGui::SliderFloat("Music Volume", &music_volume, 0.0f, 1.0f, "%.0f%%");
    ImGui::SliderFloat("Sound Effects", &sfx_volume, 0.0f, 1.0f, "%.0f%%");
    ImGui::SliderFloat("Voice Volume", &voice_volume, 0.0f, 1.0f, "%.0f%%");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Audio Output Device: Default System Output (48000 Hz, Stereo 32-bit Float)");
}

} // namespace totk::ui
