#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Volk must precede Vulkan
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include "volk.h"
#include <SDL3/SDL.h>

namespace totk::graphics {

struct GraphicsConfig {
    int window_width = 1920;
    int window_height = 1080;
    int internal_scale = 1;       // 1 = 900p, 2 = 1800p, etc.
    int aspect_ratio_mode = 0;   // 0 = 16:9, 1 = 16:10, 2 = 21:9, 3 = 32:9
    int fps_cap = 60;
    bool vsync = true;
    bool enable_fsr = true;
    float fsr_sharpness = 0.8f;
    bool validation_layers = false;
};

struct FrameStats {
    double frame_time_ms = 0.0;
    double gpu_time_ms = 0.0;
    uint64_t draw_calls = 0;
    uint64_t pipeline_binds = 0;
    uint32_t active_shaders = 0;
};

class VulkanBackend {
public:
    VulkanBackend();
    ~VulkanBackend();

    bool initialize(SDL_Window *window, const GraphicsConfig &config);
    void shutdown();

    void begin_frame();
    void end_frame();

    void resize(int width, int height);

    VkInstance get_instance() const { return instance_; }
    VkPhysicalDevice get_physical_device() const { return physical_device_; }
    VkDevice get_device() const { return device_; }
    VkQueue get_queue() const { return queue_; }
    uint32_t get_queue_family() const { return queue_family_; }
    VkRenderPass get_render_pass() const { return render_pass_; }
    VkCommandBuffer get_current_command_buffer() const { return command_buffers_[current_frame_]; }
    const FrameStats &get_stats() const { return stats_; }

private:
    bool create_instance();
    bool select_physical_device();
    bool create_device();
    bool create_swapchain();
    bool create_render_pass();
    bool create_framebuffers();
    bool create_command_pool();
    bool create_sync_objects();

    SDL_Window *window_ = nullptr;
    GraphicsConfig config_;
    FrameStats stats_;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queue_family_ = 0;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchain_format_ = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D swapchain_extent_ = {0, 0};
    std::vector<VkImage> swapchain_images_;
    std::vector<VkImageView> swapchain_views_;
    std::vector<VkFramebuffer> swapchain_framebuffers_;

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;

    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<VkCommandBuffer> command_buffers_;
    std::vector<VkSemaphore> image_available_semaphores_;
    std::vector<VkSemaphore> render_finished_semaphores_;
    std::vector<VkFence> in_flight_fences_;

    uint32_t current_frame_ = 0;
    uint32_t current_image_index_ = 0;
    bool initialized_ = false;
};

} // namespace totk::graphics
