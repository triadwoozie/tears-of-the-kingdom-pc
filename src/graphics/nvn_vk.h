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

struct CommandRecordingState {
    bool is_recording = false;
    float active_viewport_x = 0.0f;
    float active_viewport_y = 0.0f;
    float active_viewport_width = 0.0f;
    float active_viewport_height = 0.0f;
    VkRect2D active_scissor{};
    VkPipeline bound_pipeline = VK_NULL_HANDLE;
    uint64_t recorded_draws = 0;
    uint64_t recorded_draw_indexed = 0;
    uint64_t recorded_vertex_binds = 0;
    uint64_t recorded_uniform_binds = 0;
    uint64_t recorded_clears = 0;
    uint32_t last_uniform_stage = 0;
    uint32_t last_uniform_slot = 0;
    uint32_t last_uniform_offset = 0;
    size_t last_uniform_size = 0;
};

class VulkanBackend {
public:
    VulkanBackend();
    ~VulkanBackend();

    static VulkanBackend *get_active() { return s_active_backend; }
    static void set_active(VulkanBackend *backend) { s_active_backend = backend; }

    bool initialize(SDL_Window *window, const GraphicsConfig &config);
    bool initialize_mock();
    void shutdown();

    bool is_mock() const { return is_mock_; }

    void begin_frame();
    void end_frame();

    // Vulkan 1.3 Command Recording
    bool begin_command_recording();
    void end_command_recording();

    void cmd_bind_pipeline(VkPipeline pipeline, VkPipelineBindPoint bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS);
    void cmd_bind_vertex_buffers(uint32_t first_binding, uint32_t binding_count, const VkBuffer *buffers, const VkDeviceSize *offsets);
    void cmd_bind_index_buffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType index_type);
    void cmd_draw(uint32_t vertex_count, uint32_t instance_count = 1, uint32_t first_vertex = 0, uint32_t first_instance = 0);
    void cmd_draw_indexed(uint32_t index_count, uint32_t instance_count = 1, uint32_t first_index = 0, int32_t vertex_offset = 0, uint32_t first_instance = 0);
    void cmd_set_viewport(float x, float y, float width, float height, float min_depth = 0.0f, float max_depth = 1.0f);
    void cmd_set_scissor(int32_t x, int32_t y, uint32_t width, uint32_t height);
    void cmd_clear_color(VkImage image, VkImageLayout layout, const VkClearColorValue *color, uint32_t range_count, const VkImageSubresourceRange *ranges);
    void cmd_bind_uniform_buffer(uint32_t stage, uint32_t slot, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range);
    void cmd_update_uniform_buffer(uint32_t stage, uint32_t slot, const void *data, size_t size);

    bool present_texture(uint64_t queue, uint64_t window, int texture_index);

    const CommandRecordingState &get_recording_state() const { return rec_state_; }
    CommandRecordingState &get_recording_state_mut() { return rec_state_; }

    void resize(int width, int height);

    VkInstance get_instance() const { return instance_; }
    VkPhysicalDevice get_physical_device() const { return physical_device_; }
    VkDevice get_device() const { return device_; }
    VkQueue get_queue() const { return queue_; }
    uint32_t get_queue_family() const { return queue_family_; }
    VkRenderPass get_render_pass() const { return render_pass_; }
    VkCommandBuffer get_current_command_buffer() const {
        if (command_buffers_.empty()) return VK_NULL_HANDLE;
        return command_buffers_[current_frame_];
    }
    const FrameStats &get_stats() const { return stats_; }

private:
    static inline VulkanBackend *s_active_backend = nullptr;
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
    bool is_mock_ = false;
    CommandRecordingState rec_state_{};
};

} // namespace totk::graphics
