#include "nvn_vk.h"
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" int las_imgui_dyn_vertex = 0;

namespace totk::graphics {

VulkanBackend::VulkanBackend() = default;

VulkanBackend::~VulkanBackend() {
    shutdown();
}

bool VulkanBackend::initialize(SDL_Window *window, const GraphicsConfig &config) {
    window_ = window;
    config_ = config;

    if (volkInitialize() != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] Failed to initialize Volk meta-loader.\n");
        return false;
    }

    if (!create_instance()) return false;
    volkLoadInstance(instance_);

    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
        std::fprintf(stderr, "[Vulkan] Failed to create window surface: %s\n", SDL_GetError());
        return false;
    }

    if (!select_physical_device()) return false;
    if (!create_device()) return false;
    volkLoadDevice(device_);

    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);

    if (!create_swapchain()) return false;
    if (!create_render_pass()) return false;
    if (!create_framebuffers()) return false;
    if (!create_command_pool()) return false;
    if (!create_sync_objects()) return false;

    initialized_ = true;
    s_active_backend = this;
    std::printf("[Vulkan] Initialized Vulkan 1.3 backend successfully.\n");
    return true;
}

bool VulkanBackend::initialize_mock() {
    is_mock_ = true;
    initialized_ = true;
    s_active_backend = this;
    rec_state_ = {};
    stats_ = {};
    return true;
}

void VulkanBackend::shutdown() {
    if (s_active_backend == this) {
        s_active_backend = nullptr;
    }
    if (!initialized_) return;
    if (is_mock_) {
        initialized_ = false;
        is_mock_ = false;
        return;
    }

    vkDeviceWaitIdle(device_);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (render_finished_semaphores_[i]) vkDestroySemaphore(device_, render_finished_semaphores_[i], nullptr);
        if (image_available_semaphores_[i]) vkDestroySemaphore(device_, image_available_semaphores_[i], nullptr);
        if (in_flight_fences_[i]) vkDestroyFence(device_, in_flight_fences_[i], nullptr);
    }

    if (command_pool_) vkDestroyCommandPool(device_, command_pool_, nullptr);

    for (auto fb : swapchain_framebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
    swapchain_framebuffers_.clear();

    for (auto &[_, fb] : dynamic_framebuffers_) {
        if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device_, fb, nullptr);
    }
    dynamic_framebuffers_.clear();

    if (render_pass_) vkDestroyRenderPass(device_, render_pass_, nullptr);

    for (auto &[_, rp] : dynamic_render_passes_) {
        if (rp != VK_NULL_HANDLE) vkDestroyRenderPass(device_, rp, nullptr);
    }
    dynamic_render_passes_.clear();

    for (auto view : swapchain_views_) vkDestroyImageView(device_, view, nullptr);
    swapchain_views_.clear();

    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    if (surface_) SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
    if (device_) vkDestroyDevice(device_, nullptr);
    if (instance_) vkDestroyInstance(instance_, nullptr);

    initialized_ = false;
}

bool VulkanBackend::create_instance() {
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "TotK-Recomp";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "TotK Recomp Engine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 4, 3);
    app_info.apiVersion = VK_API_VERSION_1_3;

    Uint32 sdl_ext_count = 0;
    char const * const *sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_ext_count);
    if (!sdl_extensions) {
        std::fprintf(stderr, "[Vulkan] Could not query SDL instance extensions: %s\n", SDL_GetError());
        return false;
    }

    std::vector<const char *> extensions(sdl_extensions, sdl_extensions + sdl_ext_count);

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();

    const char *validation_layers[] = {"VK_LAYER_KHRONOS_validation"};
    if (config_.validation_layers) {
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = validation_layers;
    } else {
        create_info.enabledLayerCount = 0;
    }

    VkResult res = vkCreateInstance(&create_info, nullptr, &instance_);
    if (res != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] Failed to create VkInstance (code: %d)\n", res);
        return false;
    }
    return true;
}

bool VulkanBackend::select_physical_device() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) {
        std::fprintf(stderr, "[Vulkan] No physical devices with Vulkan support found.\n");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    for (const auto &dev : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);

        uint32_t q_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &q_count, nullptr);
        std::vector<VkQueueFamilyProperties> q_props(q_count);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &q_count, q_props.data());

        for (uint32_t i = 0; i < q_count; i++) {
            VkBool32 present_support = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface_, &present_support);
            if ((q_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present_support) {
                physical_device_ = dev;
                queue_family_ = i;
                std::printf("[Vulkan] Selected GPU: %s\n", props.deviceName);
                return true;
            }
        }
    }

    std::fprintf(stderr, "[Vulkan] Suitable GPU with graphics and present queue not found.\n");
    return false;
}

bool VulkanBackend::create_device() {
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info{};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = queue_family_;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    features2.pNext = &features13;

    std::vector<const char *> extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.pNext = &features2;
    create_info.queueCreateInfoCount = 1;
    create_info.pQueueCreateInfos = &queue_create_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();

    VkResult res = vkCreateDevice(physical_device_, &create_info, nullptr, &device_);
    if (res != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] Failed to create logical device (code: %d)\n", res);
        return false;
    }
    return true;
}

bool VulkanBackend::create_swapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &caps);

    swapchain_extent_ = caps.currentExtent;
    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = surface_;
    create_info.minImageCount = image_count;
    create_info.imageFormat = swapchain_format_;
    create_info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    create_info.imageExtent = swapchain_extent_;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.preTransform = caps.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = config_.vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;
    create_info.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(device_, &create_info, nullptr, &swapchain_) != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] Failed to create swapchain.\n");
        return false;
    }

    uint32_t actual_image_count = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &actual_image_count, nullptr);
    swapchain_images_.resize(actual_image_count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &actual_image_count, swapchain_images_.data());

    swapchain_views_.resize(actual_image_count);
    for (uint32_t i = 0; i < actual_image_count; i++) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = swapchain_images_[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = swapchain_format_;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &view_info, nullptr, &swapchain_views_[i]) != VK_SUCCESS) {
            return false;
        }
    }
    return true;
}

bool VulkanBackend::create_render_pass() {
    VkAttachmentDescription color_attachment{};
    color_attachment.format = swapchain_format_;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_attachment_ref{};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;

    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &color_attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;

    return vkCreateRenderPass(device_, &render_pass_info, nullptr, &render_pass_) == VK_SUCCESS;
}

bool VulkanBackend::create_framebuffers() {
    swapchain_framebuffers_.resize(swapchain_views_.size());
    for (size_t i = 0; i < swapchain_views_.size(); i++) {
        VkImageView attachments[] = {swapchain_views_[i]};

        VkFramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = render_pass_;
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.pAttachments = attachments;
        framebuffer_info.width = swapchain_extent_.width;
        framebuffer_info.height = swapchain_extent_.height;
        framebuffer_info.layers = 1;

        if (vkCreateFramebuffer(device_, &framebuffer_info, nullptr, &swapchain_framebuffers_[i]) != VK_SUCCESS) {
            return false;
        }
    }
    return true;
}

bool VulkanBackend::create_command_pool() {
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_family_;

    if (vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_) != VK_SUCCESS) {
        return false;
    }

    command_buffers_.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    return vkAllocateCommandBuffers(device_, &alloc_info, command_buffers_.data()) == VK_SUCCESS;
}

bool VulkanBackend::create_sync_objects() {
    image_available_semaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    render_finished_semaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    in_flight_fences_.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo sem_info{};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(device_, &sem_info, nullptr, &image_available_semaphores_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device_, &sem_info, nullptr, &render_finished_semaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(device_, &fence_info, nullptr, &in_flight_fences_[i]) != VK_SUCCESS) {
            return false;
        }
    }
    return true;
}

void VulkanBackend::begin_frame() {
    vkWaitForFences(device_, 1, &in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &in_flight_fences_[current_frame_]);

    vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                          image_available_semaphores_[current_frame_],
                          VK_NULL_HANDLE, &current_image_index_);

    VkCommandBuffer cmd = command_buffers_[current_frame_];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &begin_info);

    VkRenderPassBeginInfo rp_info{};
    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_info.renderPass = render_pass_;
    rp_info.framebuffer = swapchain_framebuffers_[current_image_index_];
    rp_info.renderArea.offset = {0, 0};
    rp_info.renderArea.extent = swapchain_extent_;

    VkClearValue clear_color = {{{0.07f, 0.07f, 0.09f, 1.0f}}};
    rp_info.clearValueCount = 1;
    rp_info.pClearValues = &clear_color;

    vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanBackend::end_frame() {
    VkCommandBuffer cmd = command_buffers_[current_frame_];
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore wait_semaphores[] = {image_available_semaphores_[current_frame_]};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;

    VkSemaphore signal_semaphores[] = {render_finished_semaphores_[current_frame_]};
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;

    vkQueueSubmit(queue_, 1, &submit_info, in_flight_fences_[current_frame_]);

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;
    VkSwapchainKHR swapchains[] = {swapchain_};
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swapchains;
    present_info.pImageIndices = &current_image_index_;

    vkQueuePresentKHR(queue_, &present_info);

    current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanBackend::resize(int width, int height) {
    if (!initialized_) return;
    vkDeviceWaitIdle(device_);

    for (auto fb : swapchain_framebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
    swapchain_framebuffers_.clear();
    for (auto view : swapchain_views_) vkDestroyImageView(device_, view, nullptr);
    swapchain_views_.clear();
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);

    create_swapchain();
    create_framebuffers();
    (void)width; (void)height;
}

bool VulkanBackend::begin_command_recording() {
    rec_state_.is_recording = true;
    if (!is_mock_ && initialized_ && !command_buffers_.empty()) {
        VkCommandBuffer cmd = command_buffers_[current_frame_];
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin_info);
    }
    return true;
}

void VulkanBackend::end_command_recording() {
    if (!is_mock_ && initialized_ && rec_state_.is_recording && !command_buffers_.empty()) {
        VkCommandBuffer cmd = command_buffers_[current_frame_];
        vkEndCommandBuffer(cmd);
    }
    rec_state_.is_recording = false;
}

void VulkanBackend::cmd_bind_pipeline(VkPipeline pipeline, VkPipelineBindPoint bind_point) {
    rec_state_.bound_pipeline = pipeline;
    stats_.pipeline_binds++;
    if (!is_mock_ && initialized_ && rec_state_.is_recording && !command_buffers_.empty()) {
        VkCommandBuffer cmd = command_buffers_[current_frame_];
        vkCmdBindPipeline(cmd, bind_point, pipeline);
    }
}

void VulkanBackend::cmd_bind_vertex_buffers(uint32_t first_binding, uint32_t binding_count, const VkBuffer *buffers, const VkDeviceSize *offsets) {
    rec_state_.recorded_vertex_binds += binding_count;
    if (!is_mock_ && initialized_ && rec_state_.is_recording && !command_buffers_.empty()) {
        VkCommandBuffer cmd = command_buffers_[current_frame_];
        vkCmdBindVertexBuffers(cmd, first_binding, binding_count, buffers, offsets);
    }
}

void VulkanBackend::cmd_bind_index_buffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType index_type) {
    if (!is_mock_ && initialized_ && rec_state_.is_recording && !command_buffers_.empty()) {
        VkCommandBuffer cmd = command_buffers_[current_frame_];
        vkCmdBindIndexBuffer(cmd, buffer, offset, index_type);
    }
}

void VulkanBackend::cmd_draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance) {
    rec_state_.recorded_draws++;
    stats_.draw_calls++;
    if (!is_mock_ && initialized_ && rec_state_.is_recording && !command_buffers_.empty()) {
        VkCommandBuffer cmd = command_buffers_[current_frame_];
        vkCmdDraw(cmd, vertex_count, instance_count, first_vertex, first_instance);
    }
}

void VulkanBackend::cmd_draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance) {
    rec_state_.recorded_draw_indexed++;
    rec_state_.recorded_draws++;
    stats_.draw_calls++;
    if (!is_mock_ && initialized_ && rec_state_.is_recording && !command_buffers_.empty()) {
        VkCommandBuffer cmd = command_buffers_[current_frame_];
        vkCmdDrawIndexed(cmd, index_count, instance_count, first_index, vertex_offset, first_instance);
    }
}

void VulkanBackend::cmd_set_viewport(float x, float y, float width, float height, float min_depth, float max_depth) {
    rec_state_.active_viewport_x = x;
    rec_state_.active_viewport_y = y;
    rec_state_.active_viewport_width = width;
    rec_state_.active_viewport_height = height;
    if (!is_mock_ && initialized_ && rec_state_.is_recording && !command_buffers_.empty()) {
        VkCommandBuffer cmd = command_buffers_[current_frame_];
        VkViewport vp{x, y, width, height, min_depth, max_depth};
        vkCmdSetViewport(cmd, 0, 1, &vp);
    }
}

void VulkanBackend::cmd_set_scissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    rec_state_.active_scissor.offset = {x, y};
    rec_state_.active_scissor.extent = {width, height};
    if (!is_mock_ && initialized_ && rec_state_.is_recording && !command_buffers_.empty()) {
        VkCommandBuffer cmd = command_buffers_[current_frame_];
        VkRect2D scissor{{x, y}, {width, height}};
        vkCmdSetScissor(cmd, 0, 1, &scissor);
    }
}

void VulkanBackend::cmd_clear_color(VkImage image, VkImageLayout layout, const VkClearColorValue *color, uint32_t range_count, const VkImageSubresourceRange *ranges) {
    rec_state_.recorded_clears++;
    if (!is_mock_ && initialized_ && rec_state_.is_recording && !command_buffers_.empty()) {
        VkCommandBuffer cmd = command_buffers_[current_frame_];
        vkCmdClearColorImage(cmd, image, layout, color, range_count, ranges);
    }
}

void VulkanBackend::cmd_bind_uniform_buffer(uint32_t stage, uint32_t slot, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range) {
    rec_state_.recorded_uniform_binds++;
    rec_state_.last_uniform_stage = stage;
    rec_state_.last_uniform_slot = slot;
    rec_state_.last_uniform_offset = static_cast<uint32_t>(offset);
    rec_state_.last_uniform_size = static_cast<size_t>(range);
    (void)buffer;
}

void VulkanBackend::cmd_update_uniform_buffer(uint32_t stage, uint32_t slot, const void *data, size_t size) {
    rec_state_.recorded_uniform_binds++;
    rec_state_.last_uniform_stage = stage;
    rec_state_.last_uniform_slot = slot;
    rec_state_.last_uniform_size = size;
    (void)data;
}

void VulkanBackend::cmd_dispatch_compute(uint32_t group_x, uint32_t group_y, uint32_t group_z) {
    rec_state_.recorded_dispatches++;
    rec_state_.last_dispatch_x = group_x;
    rec_state_.last_dispatch_y = group_y;
    rec_state_.last_dispatch_z = group_z;
    if (!is_mock_ && initialized_ && rec_state_.is_recording && !command_buffers_.empty()) {
        VkCommandBuffer cmd = command_buffers_[current_frame_];
        vkCmdDispatch(cmd, group_x, group_y, group_z);
    }
}

void VulkanBackend::cmd_resolve_multisample_image(VkImage src_image, VkImage dst_image, uint32_t width, uint32_t height) {
    rec_state_.recorded_resolves++;
    if (!is_mock_ && initialized_ && rec_state_.is_recording && !command_buffers_.empty() &&
        src_image != VK_NULL_HANDLE && dst_image != VK_NULL_HANDLE) {
        VkCommandBuffer cmd = command_buffers_[current_frame_];

        VkImageResolve resolve_region{};
        resolve_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        resolve_region.srcSubresource.mipLevel = 0;
        resolve_region.srcSubresource.baseArrayLayer = 0;
        resolve_region.srcSubresource.layerCount = 1;
        resolve_region.srcOffset = {0, 0, 0};

        resolve_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        resolve_region.dstSubresource.mipLevel = 0;
        resolve_region.dstSubresource.baseArrayLayer = 0;
        resolve_region.dstSubresource.layerCount = 1;
        resolve_region.dstOffset = {0, 0, 0};

        resolve_region.extent = {width, height, 1};

        vkCmdResolveImage(cmd,
                          src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          dst_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          1, &resolve_region);
    }
}

void VulkanBackend::cmd_set_render_targets(uint32_t num_colors, const RenderTargetAttachment *color_targets, const RenderTargetAttachment *depth_target) {
    rec_state_.recorded_rt_binds++;
    rec_state_.last_rt_color_count = num_colors;
    rec_state_.last_rt_has_depth = (depth_target != nullptr);
    if (color_targets && num_colors > 0) {
        rec_state_.active_rt_width = color_targets[0].width;
        rec_state_.active_rt_height = color_targets[0].height;
        for (uint32_t i = 0; i < num_colors && i < 8; ++i) {
            rec_state_.last_rt_color_formats[i] = color_targets[i].format;
        }
    }
    if (depth_target) {
        rec_state_.last_rt_depth_format = depth_target->format;
        if (rec_state_.active_rt_width == 0) {
            rec_state_.active_rt_width = depth_target->width;
            rec_state_.active_rt_height = depth_target->height;
        }
    } else {
        rec_state_.last_rt_depth_format = VK_FORMAT_UNDEFINED;
    }

    if (is_mock_ || !initialized_ || !rec_state_.is_recording || command_buffers_.empty()) {
        return;
    }

    VkImageView color_views[8]{};
    VkFormat color_formats[8]{};
    for (uint32_t i = 0; i < num_colors && i < 8; ++i) {
        color_views[i] = color_targets[i].image_view;
        color_formats[i] = color_targets[i].format;
    }
    VkImageView depth_view = depth_target ? depth_target->image_view : VK_NULL_HANDLE;
    VkFormat depth_format = depth_target ? depth_target->format : VK_FORMAT_UNDEFINED;

    VkFramebuffer fb = get_or_create_framebuffer(
        rec_state_.active_rt_width, rec_state_.active_rt_height,
        num_colors, color_views, color_formats, depth_view, depth_format);
    rec_state_.active_framebuffer = fb;
}

void VulkanBackend::cmd_set_render_targets_simple(uint32_t num_colors, const uint64_t *color_target_handles, uint64_t depth_target_handle) {
    rec_state_.recorded_rt_binds++;
    rec_state_.last_rt_color_count = num_colors;
    rec_state_.last_rt_has_depth = (depth_target_handle != 0);
    if (rec_state_.active_rt_width == 0) {
        rec_state_.active_rt_width = 1920;
        rec_state_.active_rt_height = 1080;
    }
    for (uint32_t i = 0; i < num_colors && i < 8; ++i) {
        rec_state_.last_rt_color_formats[i] = VK_FORMAT_R8G8B8A8_UNORM;
    }
    if (depth_target_handle != 0) {
        rec_state_.last_rt_depth_format = VK_FORMAT_D32_SFLOAT;
    }
    (void)color_target_handles;
}

void VulkanBackend::set_offscreen_hdr_target(VkImage image, VkFormat format, uint32_t width, uint32_t height) {
    offscreen_hdr_image_ = image;
    offscreen_hdr_format_ = format;
    offscreen_hdr_width_ = width;
    offscreen_hdr_height_ = height;
}

bool VulkanBackend::blit_hdr_to_swapchain(VkImage src_image, VkFormat src_format, uint32_t src_width, uint32_t src_height) {
    rec_state_.recorded_blits++;
    bool is_hdr = (src_format == VK_FORMAT_R16G16B16A16_SFLOAT ||
                   src_format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ||
                   src_format == VK_FORMAT_B10G11R11_UFLOAT_PACK32 ||
                   src_format == VK_FORMAT_R32G32B32A32_SFLOAT);
    rec_state_.last_blit_was_hdr = is_hdr;

    if (is_mock_ || !initialized_ || command_buffers_.empty()) {
        return true;
    }

    VkCommandBuffer cmd = command_buffers_[current_frame_];
    VkImage dst_image = (current_image_index_ < swapchain_images_.size()) ? swapchain_images_[current_image_index_] : VK_NULL_HANDLE;
    if (src_image == VK_NULL_HANDLE || dst_image == VK_NULL_HANDLE) {
        return false;
    }

    // Transition src to TRANSFER_SRC_OPTIMAL
    VkImageMemoryBarrier src_barrier{};
    src_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    src_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    src_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    src_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_barrier.image = src_image;
    src_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    src_barrier.subresourceRange.baseMipLevel = 0;
    src_barrier.subresourceRange.levelCount = 1;
    src_barrier.subresourceRange.baseArrayLayer = 0;
    src_barrier.subresourceRange.layerCount = 1;
    src_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    src_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    // Transition dst to TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier dst_barrier{};
    dst_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    dst_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    dst_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    dst_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dst_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dst_barrier.image = dst_image;
    dst_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    dst_barrier.subresourceRange.baseMipLevel = 0;
    dst_barrier.subresourceRange.levelCount = 1;
    dst_barrier.subresourceRange.baseArrayLayer = 0;
    dst_barrier.subresourceRange.layerCount = 1;
    dst_barrier.srcAccessMask = 0;
    dst_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    VkImageMemoryBarrier barriers[2] = {src_barrier, dst_barrier};
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 2, barriers);

    // Blit region
    VkImageBlit blit_region{};
    blit_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit_region.srcSubresource.mipLevel = 0;
    blit_region.srcSubresource.baseArrayLayer = 0;
    blit_region.srcSubresource.layerCount = 1;
    blit_region.srcOffsets[0] = {0, 0, 0};
    blit_region.srcOffsets[1] = {static_cast<int32_t>(src_width), static_cast<int32_t>(src_height), 1};

    blit_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit_region.dstSubresource.mipLevel = 0;
    blit_region.dstSubresource.baseArrayLayer = 0;
    blit_region.dstSubresource.layerCount = 1;
    blit_region.dstOffsets[0] = {0, 0, 0};
    blit_region.dstOffsets[1] = {static_cast<int32_t>(swapchain_extent_.width), static_cast<int32_t>(swapchain_extent_.height), 1};

    vkCmdBlitImage(cmd,
                   src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dst_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit_region, VK_FILTER_LINEAR);

    // Transition dst to PRESENT_SRC_KHR
    dst_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    dst_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    dst_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dst_barrier.dstAccessMask = 0;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &dst_barrier);

    return true;
}

VkRenderPass VulkanBackend::get_or_create_render_pass(uint32_t num_colors, const VkFormat *color_formats, VkFormat depth_format) {
    if (device_ == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    uint64_t hash = num_colors;
    for (uint32_t i = 0; i < num_colors; ++i) {
        hash = hash * 31 + static_cast<uint64_t>(color_formats[i]);
    }
    hash = hash * 31 + static_cast<uint64_t>(depth_format);

    auto it = dynamic_render_passes_.find(hash);
    if (it != dynamic_render_passes_.end()) {
        return it->second;
    }

    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference> color_refs;

    for (uint32_t i = 0; i < num_colors; ++i) {
        VkAttachmentDescription desc{};
        desc.format = color_formats[i];
        desc.samples = VK_SAMPLE_COUNT_1_BIT;
        desc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        desc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        desc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments.push_back(desc);

        VkAttachmentReference ref{};
        ref.attachment = i;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_refs.push_back(ref);
    }

    VkAttachmentReference depth_ref{};
    if (depth_format != VK_FORMAT_UNDEFINED) {
        VkAttachmentDescription desc{};
        desc.format = depth_format;
        desc.samples = VK_SAMPLE_COUNT_1_BIT;
        desc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        desc.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        desc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments.push_back(desc);

        depth_ref.attachment = static_cast<uint32_t>(attachments.size() - 1);
        depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(color_refs.size());
    subpass.pColorAttachments = color_refs.empty() ? nullptr : color_refs.data();
    subpass.pDepthStencilAttachment = (depth_format != VK_FORMAT_UNDEFINED) ? &depth_ref : nullptr;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp_info{};
    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_info.attachmentCount = static_cast<uint32_t>(attachments.size());
    rp_info.pAttachments = attachments.empty() ? nullptr : attachments.data();
    rp_info.subpassCount = 1;
    rp_info.pSubpasses = &subpass;
    rp_info.dependencyCount = 1;
    rp_info.pDependencies = &dependency;

    VkRenderPass rp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device_, &rp_info, nullptr, &rp) == VK_SUCCESS) {
        dynamic_render_passes_[hash] = rp;
    }
    return rp;
}

VkFramebuffer VulkanBackend::get_or_create_framebuffer(uint32_t width, uint32_t height, uint32_t num_colors,
                                                       const VkImageView *color_views, const VkFormat *color_formats,
                                                       VkImageView depth_view, VkFormat depth_format) {
    if (device_ == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    FramebufferKey key;
    key.width = width;
    key.height = height;
    key.num_colors = num_colors;
    for (uint32_t i = 0; i < num_colors && i < 8; ++i) {
        key.views[i] = color_views ? color_views[i] : VK_NULL_HANDLE;
    }
    key.views[num_colors] = depth_view;

    auto it = dynamic_framebuffers_.find(key);
    if (it != dynamic_framebuffers_.end()) {
        return it->second;
    }

    VkRenderPass rp = get_or_create_render_pass(num_colors, color_formats, depth_format);
    if (rp == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    std::vector<VkImageView> attachments;
    for (uint32_t i = 0; i < num_colors && i < 8; ++i) {
        attachments.push_back(color_views[i]);
    }
    if (depth_view != VK_NULL_HANDLE) {
        attachments.push_back(depth_view);
    }

    VkFramebufferCreateInfo fb_info{};
    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.renderPass = rp;
    fb_info.attachmentCount = static_cast<uint32_t>(attachments.size());
    fb_info.pAttachments = attachments.data();
    fb_info.width = width;
    fb_info.height = height;
    fb_info.layers = 1;

    VkFramebuffer fb = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(device_, &fb_info, nullptr, &fb) == VK_SUCCESS) {
        dynamic_framebuffers_[key] = fb;
    }
    return fb;
}

void VulkanBackend::queue_submit_commands(int num_cmds, const void *handles) {
    rec_state_.recorded_submits += (num_cmds > 0 ? num_cmds : 1);
    (void)handles;
    if (is_mock_ || !initialized_ || command_buffers_.empty()) {
        return;
    }
    if (rec_state_.is_recording) {
        end_command_recording();
    }
}

void VulkanBackend::queue_finish() {
    rec_state_.recorded_finishes++;
    if (!is_mock_ && initialized_ && device_ != VK_NULL_HANDLE) {
        vkQueueWaitIdle(queue_);
    }
}

void VulkanBackend::queue_flush() {
    rec_state_.recorded_flushes++;
}

bool VulkanBackend::present_texture(uint64_t queue, uint64_t window, int texture_index) {
    (void)queue;
    (void)window;
    (void)texture_index;

    rec_state_.recorded_presents++;

    if (offscreen_hdr_image_ != VK_NULL_HANDLE || offscreen_hdr_format_ != VK_FORMAT_UNDEFINED) {
        blit_hdr_to_swapchain(offscreen_hdr_image_, offscreen_hdr_format_, offscreen_hdr_width_, offscreen_hdr_height_);
    }

    if (is_mock_) {
        rec_state_.is_recording = false;
        return true;
    }

    if (!initialized_) return false;

    VkCommandBuffer cmd = command_buffers_[current_frame_];

    // Transition color attachment to present layout
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    if (current_image_index_ < swapchain_images_.size()) {
        barrier.image = swapchain_images_[current_image_index_];
    }
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = 0;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &barrier);

    end_frame();
    return true;
}

} // namespace totk::graphics
