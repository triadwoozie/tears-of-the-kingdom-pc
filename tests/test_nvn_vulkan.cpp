#include "graphics/nvn_uniform_snapshot.h"
#include "graphics/nvn_buffer_cache.h"
#include "graphics/nvn_conditional_draw.h"
#include "graphics/vk_pipeline_cache.h"
#include "graphics/nvn_vk.h"
#include "graphics/hle_nvn_gen.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

void test_uniform_snapshot_ring() {
    std::printf("[Test] Running test_uniform_snapshot_ring...\n");
    totk::graphics::NvnUniformSnapshotRing ring(1024 * 1024); // 1 MiB

    // Push first uniform chunk (e.g. ViewProjection matrix: 64 bytes)
    float view_proj[16] = {1.0f, 0.0f, 0.0f, 0.0f,
                           0.0f, 1.0f, 0.0f, 0.0f,
                           0.0f, 0.0f, 1.0f, 0.0f,
                           0.0f, 0.0f, 0.0f, 1.0f};
    uint32_t off1 = ring.push_snapshot(0, 1, view_proj, sizeof(view_proj));
    assert(off1 == 0);
    assert(ring.get_snapshot_count() == 1);

    // Push second uniform chunk (e.g. Model transform: 64 bytes)
    float model[16] = {2.0f, 0.0f, 0.0f, 0.0f,
                       0.0f, 2.0f, 0.0f, 0.0f,
                       0.0f, 0.0f, 2.0f, 0.0f,
                       5.0f, 5.0f, 5.0f, 1.0f};
    uint32_t off2 = ring.push_snapshot(1, 2, model, sizeof(model));
    // Check 256-byte alignment of uniform offsets
    assert(off2 >= 64);
    assert((off2 % 256) == 0);
    assert(ring.get_snapshot_count() == 2);

    const auto &snapshots = ring.get_snapshots();
    assert(snapshots.size() == 2);
    assert(snapshots[0].stage == 0);
    assert(snapshots[0].slot == 1);
    assert(std::memcmp(snapshots[0].data.data(), view_proj, sizeof(view_proj)) == 0);
    assert(snapshots[1].stage == 1);
    assert(snapshots[1].slot == 2);
    assert(std::memcmp(snapshots[1].data.data(), model, sizeof(model)) == 0);

    ring.reset();
    assert(ring.get_snapshot_count() == 0);
    assert(ring.get_used_bytes() == 0);

    std::printf("  -> Uniform Snapshot Ring: PASSED\n");
}

void test_buffer_cache_tracking() {
    std::printf("[Test] Running test_buffer_cache_tracking...\n");
    totk::graphics::NvnBufferCache buffer_cache;

    uint64_t fake_guest_addr1 = 0x7010000000ULL;
    size_t size1 = 65536;
    auto *alloc1 = buffer_cache.register_guest_buffer(fake_guest_addr1, size1);
    assert(alloc1 != nullptr);
    assert(alloc1->guest_addr == fake_guest_addr1);
    assert(alloc1->size == size1);
    assert(alloc1->is_dirty == true);
    assert(buffer_cache.get_tracked_buffer_count() == 1);

    // Invalidate dirty range
    buffer_cache.invalidate_range(fake_guest_addr1 + 0x100, 0x200);
    assert(alloc1->is_dirty == true);

    std::printf("  -> NVN Buffer Cache Tracking: PASSED\n");
}

void test_conditional_rendering() {
    std::printf("[Test] Running test_conditional_rendering...\n");
    totk::graphics::NvnConditionalRenderManager cond_mgr(false);

    uint64_t counter_gpu_addr = 0x7020000000ULL;

    // Report counter value = 0 (no pixels passed occlusion query)
    cond_mgr.report_counter(counter_gpu_addr, 0);

    // Enter conditional block
    cond_mgr.begin_conditional_render(VK_NULL_HANDLE, counter_gpu_addr, totk::graphics::ConditionalRenderMode::RenderIfEqual);
    assert(cond_mgr.is_in_conditional_block() == true);

    // Equal mode: render if counter != 0 (should be false since counter is 0)
    bool should_draw_eq = cond_mgr.should_execute_draw(counter_gpu_addr, totk::graphics::ConditionalRenderMode::RenderIfEqual);
    assert(should_draw_eq == false);

    bool should_draw_neq = cond_mgr.should_execute_draw(counter_gpu_addr, totk::graphics::ConditionalRenderMode::RenderIfNotEqual);
    assert(should_draw_neq == true);

    // Update counter to non-zero (occlusion query passed pixels)
    cond_mgr.report_counter(counter_gpu_addr, 1542);
    should_draw_eq = cond_mgr.should_execute_draw(counter_gpu_addr, totk::graphics::ConditionalRenderMode::RenderIfEqual);
    assert(should_draw_eq == true);

    should_draw_neq = cond_mgr.should_execute_draw(counter_gpu_addr, totk::graphics::ConditionalRenderMode::RenderIfNotEqual);
    assert(should_draw_neq == false);

    // End conditional block: draws become unconditional
    cond_mgr.end_conditional_render(VK_NULL_HANDLE);
    assert(cond_mgr.is_in_conditional_block() == false);
    assert(cond_mgr.should_execute_draw(counter_gpu_addr, totk::graphics::ConditionalRenderMode::RenderIfEqual) == true);

    std::printf("  -> NVN Conditional Rendering Manager: PASSED\n");
}

void test_pipeline_cache_progress() {
    std::printf("[Test] Running test_pipeline_cache_progress...\n");
    totk::graphics::PipelineCacheManager pcm;

    assert(pcm.get_total_count() == 0);
    assert(pcm.get_compiled_count() == 0);
    assert(pcm.get_progress() == 1.0f); // Default 1.0 when empty

    std::printf("  -> Pipeline Cache Progress: PASSED\n");
}

void test_vulkan_command_recording_and_presentation() {
    std::printf("[Test] Running test_vulkan_command_recording_and_presentation...\n");
    totk::graphics::VulkanBackend backend;
    bool init_ok = backend.initialize_mock();
    assert(init_ok);
    assert(backend.is_mock());
    assert(totk::graphics::VulkanBackend::get_active() == &backend);

    // 1. Begin recording
    backend.begin_command_recording();
    assert(backend.get_recording_state().is_recording);

    // 2. Viewport and Scissor
    backend.cmd_set_viewport(0.0f, 0.0f, 1920.0f, 1080.0f);
    assert(backend.get_recording_state().active_viewport_width == 1920.0f);
    assert(backend.get_recording_state().active_viewport_height == 1080.0f);

    backend.cmd_set_scissor(10, 20, 1900, 1040);
    assert(backend.get_recording_state().active_scissor.offset.x == 10);
    assert(backend.get_recording_state().active_scissor.offset.y == 20);
    assert(backend.get_recording_state().active_scissor.extent.width == 1900);
    assert(backend.get_recording_state().active_scissor.extent.height == 1040);

    // 3. Vertex and Index bindings
    backend.cmd_bind_vertex_buffers(0, 2, nullptr, nullptr);
    assert(backend.get_recording_state().recorded_vertex_binds == 2);

    // 4. Draw commands
    backend.cmd_draw(3, 1, 0, 0);
    assert(backend.get_recording_state().recorded_draws == 1);
    assert(backend.get_stats().draw_calls == 1);

    backend.cmd_draw_indexed(6, 1, 0, 0, 0);
    assert(backend.get_recording_state().recorded_draws == 2);
    assert(backend.get_recording_state().recorded_draw_indexed == 1);
    assert(backend.get_stats().draw_calls == 2);

    // 5. Uniform buffers and clears
    backend.cmd_update_uniform_buffer(1, 3, "data", 4);
    assert(backend.get_recording_state().recorded_uniform_binds == 1);
    assert(backend.get_recording_state().last_uniform_stage == 1);
    assert(backend.get_recording_state().last_uniform_slot == 3);

    backend.cmd_clear_color(VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, nullptr, 0, nullptr);
    assert(backend.get_recording_state().recorded_clears == 1);

    // 6. Test through HLE function pointers
    using DrawArraysFn = void (*)(uint64_t, int, int, int);
    auto draw_fn = reinterpret_cast<DrawArraysFn>(totk::graphics::nvn_get_proc_address("nvnCommandBufferDrawArrays"));
    assert(draw_fn != nullptr);
    draw_fn(0, 0, 0, 36);
    assert(backend.get_recording_state().recorded_draws == 3);

    using PresentFn = void (*)(uint64_t, uint64_t, int);
    auto present_fn = reinterpret_cast<PresentFn>(totk::graphics::nvn_get_proc_address("nvnQueuePresentTexture"));
    assert(present_fn != nullptr);
    present_fn(0, 0, 1);
    assert(!backend.get_recording_state().is_recording);
    assert(totk::graphics::get_presented_frame_count() >= 1);

    backend.shutdown();
    assert(totk::graphics::VulkanBackend::get_active() == nullptr);
    std::printf("  -> Vulkan 1.3 Command Recording & Presentation: PASSED\n");
}

void test_dynamic_render_targets_and_hdr_blit() {
    std::printf("[Test] Running test_dynamic_render_targets_and_hdr_blit...\n");
    totk::graphics::VulkanBackend backend;
    bool init_ok = backend.initialize_mock();
    assert(init_ok);

    backend.begin_command_recording();

    // 1. Configure multiple render targets (MRT) with HDR formats and depth
    totk::graphics::RenderTargetAttachment colors[2]{};
    colors[0].width = 1920;
    colors[0].height = 1080;
    colors[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    colors[0].is_depth = false;

    colors[1].width = 1920;
    colors[1].height = 1080;
    colors[1].format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    colors[1].is_depth = false;

    totk::graphics::RenderTargetAttachment depth{};
    depth.width = 1920;
    depth.height = 1080;
    depth.format = VK_FORMAT_D32_SFLOAT;
    depth.is_depth = true;

    backend.cmd_set_render_targets(2, colors, &depth);
    assert(backend.get_recording_state().recorded_rt_binds == 1);
    assert(backend.get_recording_state().last_rt_color_count == 2);
    assert(backend.get_recording_state().last_rt_has_depth == true);
    assert(backend.get_recording_state().active_rt_width == 1920);
    assert(backend.get_recording_state().active_rt_height == 1080);
    assert(backend.get_recording_state().last_rt_color_formats[0] == VK_FORMAT_R16G16B16A16_SFLOAT);
    assert(backend.get_recording_state().last_rt_color_formats[1] == VK_FORMAT_A2B10G10R10_UNORM_PACK32);
    assert(backend.get_recording_state().last_rt_depth_format == VK_FORMAT_D32_SFLOAT);

    // 2. Test offscreen HDR target blit and swapchain present
    backend.set_offscreen_hdr_target(reinterpret_cast<VkImage>(0x1234), VK_FORMAT_R16G16B16A16_SFLOAT, 1920, 1080);
    bool present_ok = backend.present_texture(0, 0, 0);
    assert(present_ok);
    assert(backend.get_recording_state().recorded_blits == 1);
    assert(backend.get_recording_state().last_blit_was_hdr == true);

    // 3. Test through HLE function pointer nvnCommandBufferSetRenderTargets
    using SetRTFn = void (*)(uint64_t, int, const uint64_t *, uint64_t, uint64_t, uint64_t);
    auto set_rt_fn = reinterpret_cast<SetRTFn>(totk::graphics::nvn_get_proc_address("nvnCommandBufferSetRenderTargets"));
    assert(set_rt_fn != nullptr);

    uint64_t fake_targets[4] = {0x1000, 0x2000, 0x3000, 0x4000};
    set_rt_fn(0, 4, fake_targets, 0, 0x5000, 0);
    assert(backend.get_recording_state().recorded_rt_binds == 2);
    assert(backend.get_recording_state().last_rt_color_count == 4);
    assert(backend.get_recording_state().last_rt_has_depth == true);
    assert(backend.get_recording_state().last_rt_color_formats[0] == VK_FORMAT_R8G8B8A8_UNORM);
    assert(backend.get_recording_state().last_rt_depth_format == VK_FORMAT_D32_SFLOAT);

    backend.shutdown();
    std::printf("  -> Dynamic Render Target Allocation & HDR Blit: PASSED\n");
}

void test_queue_submission_and_synchronization() {
    std::printf("[Test] Running test_queue_submission_and_synchronization...\n");
    totk::graphics::VulkanBackend backend;
    bool init_ok = backend.initialize_mock();
    assert(init_ok);

    backend.begin_command_recording();
    backend.cmd_draw(3, 1, 0, 0);

    // 1. Direct queue operations
    backend.queue_submit_commands(2, nullptr);
    assert(backend.get_recording_state().recorded_submits == 2);

    backend.queue_flush();
    assert(backend.get_recording_state().recorded_flushes == 1);

    backend.queue_finish();
    assert(backend.get_recording_state().recorded_finishes == 1);

    // 2. HLE dispatch through nvn_get_proc_address
    using SubmitFn = void (*)(uint64_t, int, const void *);
    auto submit_fn = reinterpret_cast<SubmitFn>(totk::graphics::nvn_get_proc_address("nvnQueueSubmitCommands"));
    assert(submit_fn != nullptr);
    submit_fn(0, 1, nullptr);
    assert(backend.get_recording_state().recorded_submits == 3);

    using FinishFn = uint64_t (*)(uint64_t);
    auto finish_fn = reinterpret_cast<FinishFn>(totk::graphics::nvn_get_proc_address("nvnQueueFinish"));
    assert(finish_fn != nullptr);
    finish_fn(0);
    assert(backend.get_recording_state().recorded_finishes == 2);

    using FlushFn = uint64_t (*)(uint64_t);
    auto flush_fn = reinterpret_cast<FlushFn>(totk::graphics::nvn_get_proc_address("nvnQueueFlush"));
    assert(flush_fn != nullptr);
    flush_fn(0);
    assert(backend.get_recording_state().recorded_flushes == 2);

    backend.shutdown();
    std::printf("  -> NVN Queue Submission & Synchronization: PASSED\n");
}

int main() {
    std::printf("==================================================\n");
    std::printf("  NVN-to-Vulkan 1.3 Subsystems Unit Tests\n");
    std::printf("==================================================\n");

    test_uniform_snapshot_ring();
    test_buffer_cache_tracking();
    test_conditional_rendering();
    test_pipeline_cache_progress();
    test_vulkan_command_recording_and_presentation();
    test_dynamic_render_targets_and_hdr_blit();
    test_queue_submission_and_synchronization();

    std::printf("\nAll NVN-to-Vulkan unit tests PASSED successfully!\n");
    return 0;
}
