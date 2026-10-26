#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Volk must precede Vulkan
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include "volk.h"
#include <SDL3/SDL.h>
#include "graphics/nvn_conditional_draw.h"
#include "graphics/nvn_uniform_snapshot.h"

namespace totk::graphics {

struct GraphicsConfig {
    int window_width = 1920;
    int window_height = 1080;
    bool headless = false;
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

struct RenderTargetAttachment {
    uint64_t texture_handle = 0;
    VkImageView image_view = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    bool is_depth = false;
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

    // Dynamic Render Targets
    uint64_t recorded_rt_binds = 0;
    uint32_t last_rt_color_count = 0;
    bool last_rt_has_depth = false;
    uint32_t active_rt_width = 0;
    uint32_t active_rt_height = 0;
    VkFormat last_rt_color_formats[8]{};
    VkFormat last_rt_depth_format = VK_FORMAT_UNDEFINED;
    VkFramebuffer active_framebuffer = VK_NULL_HANDLE;
    uint64_t recorded_blits = 0;
    bool last_blit_was_hdr = false;
    uint64_t recorded_submits = 0;
    uint64_t recorded_finishes = 0;
    uint64_t recorded_flushes = 0;
    uint64_t recorded_presents = 0;
    uint64_t recorded_conditional_renders = 0;
    uint64_t recorded_dispatches = 0;
    uint32_t last_dispatch_x = 0;
    uint32_t last_dispatch_y = 0;
    uint32_t last_dispatch_z = 0;
    uint64_t recorded_resolves = 0;
};

class VulkanBackend {
public:
    VulkanBackend();
    ~VulkanBackend();

    static VulkanBackend *get_active() { return s_active_backend; }
    static void set_active(VulkanBackend *backend) { s_active_backend = backend; }

    bool initialize(SDL_Window *window, const GraphicsConfig &config);
    bool initialize_mock();
    bool ensure_headless_render_target(uint32_t width = 0, uint32_t height = 0);
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

    // Compute Shader Dispatching
    void cmd_dispatch_compute(uint32_t group_x, uint32_t group_y, uint32_t group_z);

    // Multisample Resolve
    void cmd_resolve_multisample_image(VkImage src_image, VkImage dst_image, uint32_t width, uint32_t height);

    // Dynamic Render Target Allocation & Blit
    void cmd_set_render_targets(uint32_t num_colors, const RenderTargetAttachment *color_targets, const RenderTargetAttachment *depth_target = nullptr);
    void cmd_set_render_targets_simple(uint32_t num_colors, const uint64_t *color_target_handles, uint64_t depth_target_handle = 0);
    void set_offscreen_hdr_target(VkImage image, VkFormat format, uint32_t width, uint32_t height);
    bool blit_hdr_to_swapchain(VkImage src_image, VkFormat src_format, uint32_t src_width, uint32_t src_height);

    // NVN Queue Operations & Frame Synchronization
    void queue_submit_commands(int num_cmds, const void *handles);
    void queue_finish();
    void queue_flush();

    bool present_texture(uint64_t queue, uint64_t window, int texture_index);

    NvnConditionalRenderManager &get_conditional_render_mgr() { return cond_mgr_; }
    const NvnConditionalRenderManager &get_conditional_render_mgr() const { return cond_mgr_; }
    NvnUniformSnapshotRing &get_uniform_ring() { return uniform_ring_; }
    const NvnUniformSnapshotRing &get_uniform_ring() const { return uniform_ring_; }

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
    bool create_headless_render_target(uint32_t width, uint32_t height);
    bool create_command_pool();
    bool create_sync_objects();

    struct FramebufferKey {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t num_colors = 0;
        VkImageView views[9]{}; // up to 8 colors + 1 depth
        bool operator==(const FramebufferKey &other) const {
            if (width != other.width || height != other.height || num_colors != other.num_colors) return false;
            for (size_t i = 0; i <= num_colors; ++i) {
                if (views[i] != other.views[i]) return false;
            }
            return true;
        }
    };
    struct FramebufferKeyHash {
        size_t operator()(const FramebufferKey &k) const {
            size_t h = (static_cast<size_t>(k.width) << 32) ^ k.height ^ k.num_colors;
            for (size_t i = 0; i <= k.num_colors && i < 9; ++i) {
                h ^= reinterpret_cast<uintptr_t>(k.views[i]) + 0x9e3779b9 + (h << 6) + (h >> 2);
            }
            return h;
        }
    };

    VkFramebuffer get_or_create_framebuffer(uint32_t width, uint32_t height, uint32_t num_colors,
                                            const VkImageView *color_views, const VkFormat *color_formats,
                                            VkImageView depth_view = VK_NULL_HANDLE, VkFormat depth_format = VK_FORMAT_UNDEFINED);
    VkRenderPass get_or_create_render_pass(uint32_t num_colors, const VkFormat *color_formats, VkFormat depth_format);

    std::unordered_map<FramebufferKey, VkFramebuffer, FramebufferKeyHash> dynamic_framebuffers_;
    std::unordered_map<uint64_t, VkRenderPass> dynamic_render_passes_;

    // Offscreen HDR render target tracking for swapchain blit
    VkImage offscreen_hdr_image_ = VK_NULL_HANDLE;
    VkFormat offscreen_hdr_format_ = VK_FORMAT_UNDEFINED;
    uint32_t offscreen_hdr_width_ = 0;
    uint32_t offscreen_hdr_height_ = 0;

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
    VkImage headless_color_image_ = VK_NULL_HANDLE;
    VkDeviceMemory headless_color_memory_ = VK_NULL_HANDLE;
    VkImageView headless_color_view_ = VK_NULL_HANDLE;
    VkFramebuffer headless_color_framebuffer_ = VK_NULL_HANDLE;

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
    NvnConditionalRenderManager cond_mgr_;
    NvnUniformSnapshotRing uniform_ring_;
};

} // namespace totk::graphics
