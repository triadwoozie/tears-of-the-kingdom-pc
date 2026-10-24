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
    std::printf("[Vulkan] Initialized Vulkan 1.3 backend successfully.\n");
    return true;
}

void VulkanBackend::shutdown() {
    if (!initialized_) return;

    vkDeviceWaitIdle(device_);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (render_finished_semaphores_[i]) vkDestroySemaphore(device_, render_finished_semaphores_[i], nullptr);
        if (image_available_semaphores_[i]) vkDestroySemaphore(device_, image_available_semaphores_[i], nullptr);
        if (in_flight_fences_[i]) vkDestroyFence(device_, in_flight_fences_[i], nullptr);
    }

    if (command_pool_) vkDestroyCommandPool(device_, command_pool_, nullptr);

    for (auto fb : swapchain_framebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
    swapchain_framebuffers_.clear();

    if (render_pass_) vkDestroyRenderPass(device_, render_pass_, nullptr);

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

} // namespace totk::graphics
