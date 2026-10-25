#include "ui/studio.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

void test_config_roundtrip() {
    std::printf("[Test] Running test_config_roundtrip...\n");
    std::error_code ec;
    std::filesystem::remove("local/config.toml", ec);
    std::filesystem::create_directories("work/temporary");
    const std::string test_path = "work/temporary/test_config.toml";

    totk::ui::Studio studio1;
    // Verify defaults
    assert(studio1.get_graphics_settings().resolution_scale_idx == 1);
    assert(studio1.get_graphics_settings().vsync == true);
    assert(studio1.get_audio_settings().master_volume == 1.0f);
    assert(studio1.get_controller_settings().enable_gyro == true);

    // Modify settings
    studio1.get_graphics_settings().resolution_scale_idx = 3; // 4K
    studio1.get_graphics_settings().aspect_ratio_idx = 2;     // 21:9
    studio1.get_graphics_settings().fps_cap_idx = 3;          // Unlocked
    studio1.get_graphics_settings().vsync = false;
    studio1.get_graphics_settings().enable_fsr = true;
    studio1.get_graphics_settings().fsr_sharpness = 0.95f;
    studio1.get_graphics_settings().vulkan_device_index = 1;

    studio1.get_audio_settings().master_volume = 0.85f;
    studio1.get_audio_settings().music_volume = 0.70f;
    studio1.get_audio_settings().sfx_volume = 0.90f;
    studio1.get_audio_settings().enable_spatial = false;

    studio1.get_controller_settings().prompt_theme = 2; // PlayStation
    studio1.get_controller_settings().left_deadzone = 0.15f;
    studio1.get_controller_settings().right_deadzone = 0.12f;
    studio1.get_controller_settings().enable_gyro = false;
    studio1.get_controller_settings().gyro_sensitivity = 1.5f;
    studio1.get_controller_settings().enable_rumble = true;
    studio1.get_controller_settings().rumble_intensity = 0.65f;
    studio1.get_controller_settings().keybindings.btn_a = 10;
    studio1.get_controller_settings().keybindings.btn_b = 11;

    studio1.save_config(test_path);
    assert(std::filesystem::exists(test_path));

    // Load into new instance
    totk::ui::Studio studio2;
    studio2.load_config(test_path);

    assert(studio2.get_graphics_settings().resolution_scale_idx == 3);
    assert(studio2.get_graphics_settings().aspect_ratio_idx == 2);
    assert(studio2.get_graphics_settings().fps_cap_idx == 3);
    assert(studio2.get_graphics_settings().vsync == false);
    assert(studio2.get_graphics_settings().enable_fsr == true);
    assert(std::abs(studio2.get_graphics_settings().fsr_sharpness - 0.95f) < 0.001f);
    assert(studio2.get_graphics_settings().vulkan_device_index == 1);

    assert(std::abs(studio2.get_audio_settings().master_volume - 0.85f) < 0.001f);
    assert(std::abs(studio2.get_audio_settings().music_volume - 0.70f) < 0.001f);
    assert(std::abs(studio2.get_audio_settings().sfx_volume - 0.90f) < 0.001f);
    assert(studio2.get_audio_settings().enable_spatial == false);

    assert(studio2.get_controller_settings().prompt_theme == 2);
    assert(std::abs(studio2.get_controller_settings().left_deadzone - 0.15f) < 0.001f);
    assert(std::abs(studio2.get_controller_settings().right_deadzone - 0.12f) < 0.001f);
    assert(studio2.get_controller_settings().enable_gyro == false);
    assert(std::abs(studio2.get_controller_settings().gyro_sensitivity - 1.5f) < 0.001f);
    assert(studio2.get_controller_settings().keybindings.btn_a == 10);
    assert(studio2.get_controller_settings().keybindings.btn_b == 11);

    std::filesystem::remove(test_path, ec);
    std::filesystem::remove("local/config.toml", ec);
    std::printf("  -> Config TOML Serialization Round-trip: PASSED\n");
}

void test_guest_threads_and_symbols() {
    std::printf("[Test] Running test_guest_threads_and_symbols...\n");
    totk::ui::Studio studio;

    // 1. Thread Inspector
    std::vector<totk::ui::ActiveGuestThread> threads;
    totk::ui::ActiveGuestThread t1;
    t1.thread_id = 1;
    t1.name = "MainThread";
    t1.pc = 0x7104b9e5e8ULL;
    t1.sp = 0x7000100000ULL;
    t1.priority = 16;
    t1.state = "Running";
    threads.push_back(t1);

    totk::ui::ActiveGuestThread t2;
    t2.thread_id = 2;
    t2.name = "RenderThread";
    t2.pc = 0x710c503f60ULL;
    t2.sp = 0x7000200000ULL;
    t2.priority = 14;
    t2.state = "WaitSync";
    threads.push_back(t2);

    studio.set_active_threads(threads);
    assert(studio.get_active_threads().size() == 2);
    assert(studio.get_active_threads()[0].name == "MainThread");
    assert(studio.get_active_threads()[1].name == "RenderThread");

    // 2. Symbol Table Lookup
    const std::string sym_path = "work/temporary/test_symbols.tsv";
    {
        std::ofstream sym_out(sym_path);
        sym_out << "0x7104b9e5e8\tsead::GameFrameworkNx::mainLoop\n";
        sym_out << "0x710c503f60\tnvnBootstrapLoader\n";
    }

    studio.load_symbols(sym_path);
    std::string s1 = studio.lookup_symbol(0x7104b9e5e8ULL);
    assert(s1 == "sead::GameFrameworkNx::mainLoop");

    std::string s2 = studio.lookup_symbol(0x710c503f60ULL);
    assert(s2 == "nvnBootstrapLoader");

    std::string s_off = studio.lookup_symbol(0x7104b9e5f0ULL);
    assert(s_off.rfind("sead::GameFrameworkNx::mainLoop+", 0) == 0);

    // 3. Trace Log Ringbuffer
    for (int i = 0; i < 50; i++) {
        studio.add_trace_log("Iteration log trace step #" + std::to_string(i));
    }
    assert(studio.get_trace_log_ringbuffer().size() == 50);

    std::printf("  -> Guest Threads & Symbol Inspector: PASSED\n");
}

int main() {
    std::printf("==================================================\n");
    std::printf("  UI Studio Diagnostics & Config Unit Tests\n");
    std::printf("==================================================\n");

    test_config_roundtrip();
    test_guest_threads_and_symbols();

    std::printf("\nAll UI Config & Diagnostics unit tests PASSED successfully!\n");
    return 0;
}
