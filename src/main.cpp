#include <chrono>
#include <cstdio>
#include <thread>

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include "volk.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_vulkan.h"

#include "graphics/nvn_vk.h"
#include "ui/imgui_style.h"
#include "ui/overlay.h"
#include "ui/studio.h"

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    std::printf("=====================================================\n");
    std::printf("  TotK-Recomp: The Legend of Zelda: TotK (v1.4.3)\n");
    std::printf("  Native Desktop Recompiler & Vulkan 1.3 HLE Runtime\n");
    std::printf("=====================================================\n");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "[Fatal] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    totk::graphics::GraphicsConfig config;
    config.window_width = 1600;
    config.window_height = 900;
    config.vsync = true;

    SDL_Window *window = SDL_CreateWindow(
        "TotK-Recomp - The Legend of Zelda: Tears of the Kingdom (v1.4.3)",
        config.window_width,
        config.window_height,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );

    if (!window) {
        std::fprintf(stderr, "[Fatal] Failed to create SDL window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    totk::graphics::VulkanBackend backend;
    if (!backend.initialize(window, config)) {
        std::fprintf(stderr, "[Fatal] Failed to initialize Vulkan backend.\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Initialize Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    totk::ui::apply_modern_dark_theme();

    ImGui_ImplSDL3_InitForVulkan(window);

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.ApiVersion = VK_API_VERSION_1_3;
    init_info.Instance = backend.get_instance();
    init_info.PhysicalDevice = backend.get_physical_device();
    init_info.Device = backend.get_device();
    init_info.QueueFamily = backend.get_queue_family();
    init_info.Queue = backend.get_queue();
    init_info.DescriptorPoolSize = 1000;
    init_info.RenderPass = backend.get_render_pass();
    init_info.MinImageCount = 2;
    init_info.ImageCount = 2;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&init_info)) {
        std::fprintf(stderr, "[Fatal] ImGui Vulkan init failed.\n");
        backend.shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    totk::ui::Studio studio;
    totk::ui::Overlay overlay;
    bool studio_open = true;
    bool game_running = false;
    bool running = true;

    auto last_time = std::chrono::high_resolution_clock::now();

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                int w = event.window.data1;
                int h = event.window.data2;
                backend.resize(w, h);
            }
            if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_F1) {
                    overlay.toggle();
                }
            }
        }

        auto current_time = std::chrono::high_resolution_clock::now();
        double frame_time_ms = std::chrono::duration<double, std::milli>(current_time - last_time).count();
        last_time = current_time;

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        bool launch_requested = false;
        if (studio_open) {
            studio.render(&studio_open, &launch_requested);
            if (launch_requested) {
                game_running = true;
                overlay.set_visible(true);
            }
        }

        overlay.render(frame_time_ms, 2.1); // simulated initial GPU time

        ImGui::Render();

        backend.begin_frame();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), backend.get_current_command_buffer());
        backend.end_frame();
    }

    vkDeviceWaitIdle(backend.get_device());
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    backend.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::printf("[TotK-Recomp] Graceful shutdown complete.\n");
    return 0;
}
