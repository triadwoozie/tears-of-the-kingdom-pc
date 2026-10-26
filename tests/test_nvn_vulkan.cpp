#include "graphics/nvn_uniform_snapshot.h"
#include "graphics/nvn_buffer_cache.h"
#include "graphics/nvn_conditional_draw.h"
#include "graphics/vk_pipeline_cache.h"
#include "graphics/bnsh_parser.h"
#include "graphics/nvn_bindless.h"
#include "graphics/nvn_render_states.h"
#include "graphics/bc_decoder.h"
#include "graphics/nvn_texture_cache.h"
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

void test_gameframework_render_loop_10_frames() {
    std::printf("[Test] Running test_gameframework_render_loop_10_frames...\n");
    totk::graphics::VulkanBackend backend;
    bool init_ok = backend.initialize_mock();
    assert(init_ok);

    // Function pointers resolved via nvn_get_proc_address
    using WindowInitFn = uint64_t (*)(uint64_t, uint64_t);
    using WindowAcquireFn = uint64_t (*)(uint64_t, uint64_t, uint64_t);
    using PresentFn = void (*)(uint64_t, uint64_t, int);
    using FenceSyncFn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t);
    using SyncWaitFn = int (*)(uint64_t, uint64_t);
    using SyncInitFn = uint32_t (*)(uint64_t, uint64_t);

    auto win_init = reinterpret_cast<WindowInitFn>(totk::graphics::nvn_get_proc_address("nvnWindowInitialize"));
    auto win_acquire = reinterpret_cast<WindowAcquireFn>(totk::graphics::nvn_get_proc_address("nvnWindowAcquireTexture"));
    auto present_fn = reinterpret_cast<PresentFn>(totk::graphics::nvn_get_proc_address("nvnQueuePresentTexture"));
    auto fence_sync = reinterpret_cast<FenceSyncFn>(totk::graphics::nvn_get_proc_address("nvnQueueFenceSync"));
    auto sync_wait = reinterpret_cast<SyncWaitFn>(totk::graphics::nvn_get_proc_address("nvnSyncWait"));
    auto sync_init = reinterpret_cast<SyncInitFn>(totk::graphics::nvn_get_proc_address("nvnSyncInitialize"));

    assert(win_init && win_acquire && present_fn && fence_sync && sync_wait && sync_init);

    // Initialize mock window with 3 swapchain textures
    struct MockBuilder {
        void *device = nullptr;
        void *native_win = nullptr;
        int num_textures = 3;
        void *textures[8]{(void *)0x10, (void *)0x20, (void *)0x30};
        int present_interval = 1;
    } builder;

    struct alignas(8) MockWindow {
        uint32_t magic;
        void *native_win;
        int num_textures;
        void *textures[8];
        int present_interval;
        int next_texture_idx;
    } window{};

    win_init(reinterpret_cast<uint64_t>(&window), reinterpret_cast<uint64_t>(&builder));
    assert(window.num_textures == 3);

    // Initialize NVNsync object
    uint64_t sync_obj[4]{};
    sync_init(reinterpret_cast<uint64_t>(sync_obj), 0);

    // Run 10 consecutive GameFrameworkNx frames
    uint64_t initial_presents = totk::graphics::get_presented_frame_count();
    for (int frame = 0; frame < 10; ++frame) {
        int texture_idx = -1;
        win_acquire(reinterpret_cast<uint64_t>(&window), 0, reinterpret_cast<uint64_t>(&texture_idx));
        assert(texture_idx >= 0 && texture_idx < 3);

        backend.begin_command_recording();
        backend.cmd_set_viewport(0, 0, 1920, 1080);
        backend.cmd_set_scissor(0, 0, 1920, 1080);

        // Update camera / game view uniforms
        float frame_data[4] = {static_cast<float>(frame), 16.666f, 1920.0f, 1080.0f};
        backend.cmd_update_uniform_buffer(0, 0, frame_data, sizeof(frame_data));

        // Draw HUD / game world geometry
        backend.cmd_draw(6, 1, 0, 0);
        backend.end_command_recording();

        // Queue submission & synchronization
        backend.queue_submit_commands(1, nullptr);
        fence_sync(0, reinterpret_cast<uint64_t>(sync_obj), 0, 0);

        int wait_res = sync_wait(reinterpret_cast<uint64_t>(sync_obj), 1000000000ULL);
        assert(wait_res == 0); // NVN_SYNC_WAIT_RESULT_ALREADY_SIGNALED

        // Present to display
        present_fn(0, reinterpret_cast<uint64_t>(&window), texture_idx);
    }

    assert(totk::graphics::get_presented_frame_count() >= initial_presents + 10);
    assert(backend.get_recording_state().recorded_presents >= 10);

    backend.shutdown();
    std::printf("  -> Synthetic 10-Frame GameFrameworkNx Render Loop: PASSED\n");
}

void test_bnsh_parsing_and_fallback_pipeline() {
    std::printf("[Test] Running test_bnsh_parsing_and_fallback_pipeline...\n");

    // 1. Create a synthetic BNSH container with Vertex, Fragment, and Compute stages
    totk::graphics::BnshProgram prog;
    prog.program_id = 0;

    totk::graphics::BnshStageData vs_stage;
    vs_stage.stage = totk::graphics::BnshStageKind::Vertex;
    vs_stage.control_bytes = {0x01, 0x02, 0x03, 0x04};
    vs_stage.code_bytes = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    prog.stages.push_back(vs_stage);

    totk::graphics::BnshStageData fs_stage;
    fs_stage.stage = totk::graphics::BnshStageKind::Fragment;
    fs_stage.control_bytes = {0x05, 0x06, 0x07, 0x08};
    fs_stage.code_bytes = {0xaa, 0xbb, 0xcc, 0xdd};
    prog.stages.push_back(fs_stage);

    totk::graphics::BnshStageData cs_stage;
    cs_stage.stage = totk::graphics::BnshStageKind::Compute;
    cs_stage.control_bytes.resize(0x760, 0);
    // Write compute workgroup (8, 8, 1, 2048 bytes shared memory) at offset 0x748
    uint32_t *wg = reinterpret_cast<uint32_t *>(cs_stage.control_bytes.data() + 0x748);
    wg[0] = 8;
    wg[1] = 8;
    wg[2] = 1;
    wg[3] = 2048;
    cs_stage.code_bytes = {0x99, 0x88, 0x77, 0x66};
    prog.stages.push_back(cs_stage);

    std::vector<totk::graphics::BnshProgram> input_progs = {prog};
    std::vector<uint8_t> bnsh_bytes = totk::graphics::BnshParser::create_synthetic_bnsh(input_progs);
    assert(!bnsh_bytes.empty());

    // 2. Parse back and validate
    assert(totk::graphics::BnshParser::validate_header(bnsh_bytes.data(), bnsh_bytes.size()));

    std::vector<totk::graphics::BnshProgram> parsed_progs;
    bool parse_ok = totk::graphics::BnshParser::parse(bnsh_bytes.data(), bnsh_bytes.size(), parsed_progs);
    assert(parse_ok);
    assert(parsed_progs.size() == 1);
    assert(parsed_progs[0].has_vertex);
    assert(parsed_progs[0].has_fragment);
    assert(parsed_progs[0].has_compute);
    assert(parsed_progs[0].stages.size() == 3);

    // Verify stage contents
    assert(parsed_progs[0].stages[0].code_bytes == vs_stage.code_bytes);
    assert(parsed_progs[0].stages[1].code_bytes == fs_stage.code_bytes);
    assert(parsed_progs[0].stages[2].workgroup_x == 8);
    assert(parsed_progs[0].stages[2].workgroup_y == 8);
    assert(parsed_progs[0].stages[2].workgroup_z == 1);
    assert(parsed_progs[0].stages[2].shared_memory_bytes == 2048);

    // 3. Fallback Pipeline Creation
    size_t vs_words = 0, fs_words = 0;
    const uint32_t *vs_spv = totk::graphics::BnshParser::get_fallback_vertex_spirv(vs_words);
    const uint32_t *fs_spv = totk::graphics::BnshParser::get_fallback_fragment_spirv(fs_words);
    assert(vs_spv != nullptr && vs_words > 0);
    assert(fs_spv != nullptr && fs_words > 0);
    assert(vs_spv[0] == 0x07230203);
    assert(fs_spv[0] == 0x07230203);

    VkPipeline fallback_pso = totk::graphics::BnshParser::create_fallback_pipeline(
        VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, 1920, 1080
    );
    assert(fallback_pso != VK_NULL_HANDLE);

    std::printf("  -> BNSH Header Parser & Fallback SPIR-V Pipeline: PASSED\n");
}

void test_nvn_bindless_descriptor_pool() {
    std::printf("[Test] Running test_nvn_bindless_descriptor_pool...\n");
    auto &mgr = totk::graphics::NvnBindlessManager::instance();
    mgr.reset();

    // 1. Test Handle Packing & Unpacking
    uint32_t tex_id = 0x1234;
    uint32_t samp_id = 0x567;
    uint64_t handle = totk::graphics::NvnBindlessManager::pack_texture_handle(tex_id, samp_id);
    assert(totk::graphics::NvnBindlessManager::unpack_texture_id(handle) == tex_id);
    assert(totk::graphics::NvnBindlessManager::unpack_sampler_id(handle) == samp_id);

    uint64_t tf_handle = totk::graphics::NvnBindlessManager::pack_texel_fetch_handle(tex_id);
    assert(totk::graphics::NvnBindlessManager::unpack_texture_id(tf_handle) == tex_id);
    assert(totk::graphics::NvnBindlessManager::unpack_sampler_id(tf_handle) == 0);

    // 2. Test Pool Binding
    uint64_t tex_pool_ptr = 0x7180000000ULL;
    uint64_t samp_pool_ptr = 0x7180100000ULL;
    mgr.bind_texture_pool(tex_pool_ptr, 0x1000, 256);
    mgr.bind_sampler_pool(samp_pool_ptr, 0x2000, 64);
    assert(mgr.get_bound_texture_pool() == tex_pool_ptr);
    assert(mgr.get_bound_texture_offset() == 0x1000);
    assert(mgr.get_bound_texture_size() == 256);
    assert(mgr.get_bound_sampler_pool() == samp_pool_ptr);
    assert(mgr.get_bound_sampler_offset() == 0x2000);
    assert(mgr.get_bound_sampler_size() == 64);
    assert(mgr.get_bind_texture_pool_count() == 1);
    assert(mgr.get_bind_sampler_pool_count() == 1);

    // 3. Test Registration
    mgr.register_texture(10, 0x7190000000ULL, 0x7190000100ULL);
    mgr.register_sampler(5, 0x7190000200ULL);
    assert(mgr.get_registered_texture_count() == 1);
    assert(mgr.get_registered_sampler_count() == 1);

    const auto *tex_entry = mgr.get_texture(10);
    assert(tex_entry != nullptr);
    assert(tex_entry->nvn_texture_ptr == 0x7190000000ULL);
    assert(tex_entry->nvn_view_ptr == 0x7190000100ULL);
    assert(tex_entry->is_valid);

    const auto *samp_entry = mgr.get_sampler(5);
    assert(samp_entry != nullptr);
    assert(samp_entry->nvn_sampler_ptr == 0x7190000200ULL);
    assert(samp_entry->is_valid);

    // 4. Test HLE symbol resolution and invocation
    using GetTexHandleFn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t);
    using BindPoolFn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t);
    using RegTexFn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

    auto *pfn_get_tex = reinterpret_cast<GetTexHandleFn>(totk::graphics::nvn_get_proc_address("nvnDeviceGetTextureHandle"));
    auto *pfn_bind_tex_pool = reinterpret_cast<BindPoolFn>(totk::graphics::nvn_get_proc_address("nvnCommandBufferBindTexturePool"));
    auto *pfn_bind_samp_pool = reinterpret_cast<BindPoolFn>(totk::graphics::nvn_get_proc_address("nvnCommandBufferBindSamplerPool"));
    auto *pfn_reg_tex = reinterpret_cast<RegTexFn>(totk::graphics::nvn_get_proc_address("nvnTexturePoolRegisterTexture"));

    assert(pfn_get_tex != nullptr);
    assert(pfn_bind_tex_pool != nullptr);
    assert(pfn_bind_samp_pool != nullptr);
    assert(pfn_reg_tex != nullptr);

    uint64_t res_handle = pfn_get_tex(0, 42, 7, 0);
    assert(totk::graphics::NvnBindlessManager::unpack_texture_id(res_handle) == 42);
    assert(totk::graphics::NvnBindlessManager::unpack_sampler_id(res_handle) == 7);
    assert(mgr.get_handle_query_count() > 0);

    pfn_bind_tex_pool(0, 0x7188000000ULL, 0, 0);
    assert(mgr.get_bound_texture_pool() == 0x7188000000ULL);

    pfn_bind_samp_pool(0, 0x7189000000ULL, 0, 0);
    assert(mgr.get_bound_sampler_pool() == 0x7189000000ULL);

    pfn_reg_tex(0x7188000000ULL, 99, 0x71A0000000ULL, 0x71A0000080ULL, 0);
    assert(mgr.get_texture(99) != nullptr);
    assert(mgr.get_texture(99)->nvn_texture_ptr == 0x71A0000000ULL);

    // 5. Test Mock Vulkan Init and Command Bind
    bool init_ok = mgr.initialize(VK_NULL_HANDLE);
    assert(init_ok);
    assert(mgr.is_initialized());
    mgr.cmd_bind_bindless_set(VK_NULL_HANDLE, VK_NULL_HANDLE);

    mgr.shutdown();
    assert(!mgr.is_initialized());

    std::printf("  -> NVN Bindless Descriptor Pool & Handle Manager: PASSED\n");
}

void test_nvn_render_state_tracker() {
    std::printf("[Test] Running test_nvn_render_state_tracker...\n");
    auto &state_mgr = totk::graphics::NvnRenderStateManager::instance();
    state_mgr.reset();

    uint64_t hash_default = state_mgr.compute_hash();
    assert(hash_default != 0);

    // 1. Blend State Binding
    struct RawBlend {
        uint64_t target : 3;
        uint64_t src_rgb : 7;
        uint64_t dst_rgb : 7;
        uint64_t src_a : 7;
        uint64_t dst_a : 7;
        uint64_t eq_rgb : 3;
        uint64_t eq_a : 3;
    } raw_blend{};
    raw_blend.target = 0;
    raw_blend.src_rgb = 0x5; // SRC_ALPHA
    raw_blend.dst_rgb = 0x6; // ONE_MINUS_SRC_ALPHA
    raw_blend.src_a = 0x2;   // ONE
    raw_blend.dst_a = 0x1;   // ZERO
    raw_blend.eq_rgb = 0x1;  // ADD
    raw_blend.eq_a = 0x1;    // ADD

    using BindStateFn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t);
    auto *pfn_bind_blend = reinterpret_cast<BindStateFn>(totk::graphics::nvn_get_proc_address("nvnCommandBufferBindBlendState"));
    assert(pfn_bind_blend != nullptr);
    pfn_bind_blend(0, reinterpret_cast<uint64_t>(&raw_blend), 0, 0);

    assert(state_mgr.get_bind_blend_count() == 1);
    const auto &target0 = state_mgr.get_blend_target(0);
    assert(target0.src_color_factor == VK_BLEND_FACTOR_SRC_ALPHA);
    assert(target0.dst_color_factor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    assert(target0.color_blend_op == VK_BLEND_OP_ADD);
    assert(target0.src_alpha_factor == VK_BLEND_FACTOR_ONE);
    assert(target0.dst_alpha_factor == VK_BLEND_FACTOR_ZERO);

    // 2. Channel Mask State Binding
    uint32_t channel_mask = 0xF | (0x1 << 4); // Target 0 = RGBA (0xF), Target 1 = R only (0x1)
    auto *pfn_bind_chmask = reinterpret_cast<BindStateFn>(totk::graphics::nvn_get_proc_address("nvnCommandBufferBindChannelMaskState"));
    assert(pfn_bind_chmask != nullptr);
    pfn_bind_chmask(0, reinterpret_cast<uint64_t>(&channel_mask), 0, 0);

    assert(state_mgr.get_bind_channel_mask_count() == 1);
    assert(state_mgr.get_blend_target(0).color_write_mask == (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT));
    assert(state_mgr.get_blend_target(1).color_write_mask == VK_COLOR_COMPONENT_R_BIT);

    // 3. Depth-Stencil State Binding
    struct RawDS {
        uint64_t dt : 1;
        uint64_t dw : 1;
        uint64_t df : 4;
        uint64_t st : 1;
        uint64_t sf_f : 4;
        uint64_t sf_b : 4;
        uint64_t fail_f : 4;
        uint64_t zfail_f : 4;
        uint64_t zpass_f : 4;
        uint64_t fail_b : 4;
        uint64_t zfail_b : 4;
        uint64_t zpass_b : 4;
    } raw_ds{};
    raw_ds.dt = 1; // Depth test enable
    raw_ds.dw = 1; // Depth write enable
    raw_ds.df = 0x4; // LEQUAL (NVN_DEPTH_FUNC_LEQUAL)
    raw_ds.st = 1; // Stencil test enable

    auto *pfn_bind_ds = reinterpret_cast<BindStateFn>(totk::graphics::nvn_get_proc_address("nvnCommandBufferBindDepthStencilState"));
    assert(pfn_bind_ds != nullptr);
    pfn_bind_ds(0, reinterpret_cast<uint64_t>(&raw_ds), 0, 0);

    assert(state_mgr.get_bind_depth_stencil_count() == 1);
    assert(state_mgr.get_depth_stencil().depth_test_enable == true);
    assert(state_mgr.get_depth_stencil().depth_write_enable == true);
    assert(state_mgr.get_depth_stencil().depth_compare_op == VK_COMPARE_OP_LESS_OR_EQUAL);
    assert(state_mgr.get_depth_stencil().stencil_test_enable == true);

    // 4. Polygon State Binding
    struct RawPoly {
        uint32_t cull : 2;
        uint32_t front : 1;
        uint32_t mode : 2;
        uint32_t offset_en : 3;
    } raw_poly{};
    raw_poly.cull = 0x2; // BACK
    raw_poly.front = 0x1; // CCW
    raw_poly.mode = 0x2; // FILL

    auto *pfn_bind_poly = reinterpret_cast<BindStateFn>(totk::graphics::nvn_get_proc_address("nvnCommandBufferBindPolygonState"));
    assert(pfn_bind_poly != nullptr);
    pfn_bind_poly(0, reinterpret_cast<uint64_t>(&raw_poly), 0, 0);

    assert(state_mgr.get_bind_polygon_count() == 1);
    assert(state_mgr.get_rasterizer().cull_mode == VK_CULL_MODE_BACK_BIT);
    assert(state_mgr.get_rasterizer().front_face == VK_FRONT_FACE_COUNTER_CLOCKWISE);
    assert(state_mgr.get_rasterizer().polygon_mode == VK_POLYGON_MODE_FILL);

    // 5. Multisample State Binding
    struct RawMS {
        uint8_t enable, samples, a2c, a2c_dither, raster_samples, cov_mod, cov2color_en, cov2color_out, sl_en, sl_grid;
    } raw_ms{};
    raw_ms.enable = 1;
    raw_ms.samples = 4;
    raw_ms.a2c = 1;

    auto *pfn_bind_ms = reinterpret_cast<BindStateFn>(totk::graphics::nvn_get_proc_address("nvnCommandBufferBindMultisampleState"));
    assert(pfn_bind_ms != nullptr);
    pfn_bind_ms(0, reinterpret_cast<uint64_t>(&raw_ms), 0, 0);

    assert(state_mgr.get_bind_multisample_count() == 1);
    assert(state_mgr.get_multisample().rasterization_samples == VK_SAMPLE_COUNT_4_BIT);
    assert(state_mgr.get_multisample().alpha_to_coverage_enable == true);

    // 6. Dynamic State Application (safe no-op with null cmd)
    state_mgr.apply_dynamic_states(VK_NULL_HANDLE);

    // 7. Verify State Hash updated
    uint64_t hash_configured = state_mgr.compute_hash();
    assert(hash_configured != hash_default);

    std::printf("  -> NVN Render State Tracker (Blend, Depth-Stencil, Rasterizer, Mask, MSAA): PASSED\n");
}

void test_compute_views_and_bc_decoders() {
    std::printf("[Test] Running test_compute_views_and_bc_decoders...\n");

    // 1. Compute Shader Dispatch & Multisample Resolve in Mock Backend
    totk::graphics::VulkanBackend backend;
    bool init_ok = backend.initialize_mock();
    assert(init_ok);

    backend.begin_command_recording();
    backend.cmd_dispatch_compute(8, 4, 2);
    assert(backend.get_recording_state().recorded_dispatches == 1);
    assert(backend.get_recording_state().last_dispatch_x == 8);
    assert(backend.get_recording_state().last_dispatch_y == 4);
    assert(backend.get_recording_state().last_dispatch_z == 2);

    backend.cmd_resolve_multisample_image(reinterpret_cast<VkImage>(0x100), reinterpret_cast<VkImage>(0x200), 1920, 1080);
    assert(backend.get_recording_state().recorded_resolves == 1);

    // Call compute dispatch via NVN HLE symbol
    using DispatchFn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
    auto pfn_dispatch = reinterpret_cast<DispatchFn>(totk::graphics::nvn_get_proc_address("nvnCommandBufferDispatchCompute"));
    assert(pfn_dispatch != nullptr);
    pfn_dispatch(0, 16, 16, 1, 0, 0, 0, 0);
    assert(backend.get_recording_state().recorded_dispatches == 2);
    assert(backend.get_recording_state().last_dispatch_x == 16);
    assert(backend.get_recording_state().last_dispatch_y == 16);
    assert(backend.get_recording_state().last_dispatch_z == 1);

    backend.end_command_recording();
    backend.shutdown();

    // 2. Texture View Internal configuration
    totk::graphics::NvnTextureViewInternal view{};
    using SetDefaultsFn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
    using SetLevelsFn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
    using SetLayersFn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
    using SetFormatFn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
    using SetSwizzleFn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
    using SetTargetFn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

    auto pfn_view_defaults = reinterpret_cast<SetDefaultsFn>(totk::graphics::nvn_get_proc_address("nvnTextureViewSetDefaults"));
    auto pfn_view_levels = reinterpret_cast<SetLevelsFn>(totk::graphics::nvn_get_proc_address("nvnTextureViewSetLevels"));
    auto pfn_view_layers = reinterpret_cast<SetLayersFn>(totk::graphics::nvn_get_proc_address("nvnTextureViewSetLayers"));
    auto pfn_view_format = reinterpret_cast<SetFormatFn>(totk::graphics::nvn_get_proc_address("nvnTextureViewSetFormat"));
    auto pfn_view_swizzle = reinterpret_cast<SetSwizzleFn>(totk::graphics::nvn_get_proc_address("nvnTextureViewSetSwizzle"));
    auto pfn_view_target = reinterpret_cast<SetTargetFn>(totk::graphics::nvn_get_proc_address("nvnTextureViewSetTarget"));

    assert(pfn_view_defaults && pfn_view_levels && pfn_view_layers && pfn_view_format && pfn_view_swizzle && pfn_view_target);

    pfn_view_defaults(reinterpret_cast<uint64_t>(&view), 0, 0, 0, 0, 0, 0, 0);
    pfn_view_levels(reinterpret_cast<uint64_t>(&view), 2, 4, 0, 0, 0, 0, 0);
    pfn_view_layers(reinterpret_cast<uint64_t>(&view), 1, 6, 0, 0, 0, 0, 0);
    pfn_view_format(reinterpret_cast<uint64_t>(&view), 0x25, 0, 0, 0, 0, 0, 0);
    pfn_view_swizzle(reinterpret_cast<uint64_t>(&view), 0, 1, 2, 3, 0, 0, 0);
    pfn_view_target(reinterpret_cast<uint64_t>(&view), 3, 0, 0, 0, 0, 0, 0);

    assert(view.set_levels == 1 && view.base_level == 2 && view.nlevels == 4);
    assert(view.set_layers == 1 && view.min_layer == 1 && view.nlayers == 6);
    assert(view.set_format == 1 && view.format == 0x25);
    assert(view.set_target == 1 && view.target == 3);

    // 3. BC Decoders (BC1, BC3, BC4, BC5, BC7)
    // BC1 synthetic 4x4 block
    // c0 = 0xF800 (Pure Red in RGB565: R=31, G=0, B=0)
    // c1 = 0x001F (Pure Blue in RGB565: R=0, G=0, B=31)
    // c0 > c1 (0xF800 > 0x001F)
    // All indices 0 -> all 16 pixels are color0 (Red)
    uint8_t bc1_block[8] = {
        0x00, 0xF8, // c0 = 0xF800
        0x1F, 0x00, // c1 = 0x001F
        0x00, 0x00, 0x00, 0x00 // indices = 0
    };
    uint8_t rgba_out[4 * 4 * 4];
    std::memset(rgba_out, 0, sizeof(rgba_out));
    totk::graphics::BcDecoder::decode_bc1(bc1_block, 4, 4, rgba_out);

    // Verify all 16 pixels are decoded to Red (R ~ 255, G = 0, B = 0, A = 255)
    for (int p = 0; p < 16; ++p) {
        assert(rgba_out[p * 4 + 0] >= 250); // Red
        assert(rgba_out[p * 4 + 1] == 0);   // Green
        assert(rgba_out[p * 4 + 2] == 0);   // Blue
        assert(rgba_out[p * 4 + 3] == 255); // Alpha
    }

    // BC3 synthetic 4x4 block (8 bytes alpha + 8 bytes color)
    uint8_t bc3_block[16];
    std::memset(bc3_block, 0, sizeof(bc3_block));
    bc3_block[0] = 200; // a0
    bc3_block[1] = 50;  // a1
    // alpha indices 0 -> all alpha = a0 = 200
    bc3_block[8] = 0x00; bc3_block[9] = 0xF8; // Red color
    std::memset(rgba_out, 0, sizeof(rgba_out));
    totk::graphics::BcDecoder::decode_bc3(bc3_block, 4, 4, rgba_out);
    assert(rgba_out[0] >= 250);
    assert(rgba_out[3] == 200); // Alpha matches a0

    // BC4 synthetic 4x4 block (8 bytes red channel)
    uint8_t bc4_block[8];
    std::memset(bc4_block, 0, sizeof(bc4_block));
    bc4_block[0] = 180; // v0
    bc4_block[1] = 40;  // v1
    std::memset(rgba_out, 0, sizeof(rgba_out));
    totk::graphics::BcDecoder::decode_bc4(bc4_block, 4, 4, rgba_out);
    assert(rgba_out[0] == 180); // Red
    assert(rgba_out[1] == 0);   // Green = 0
    assert(rgba_out[2] == 0);   // Blue = 0
    assert(rgba_out[3] == 255); // Alpha = 255

    // BC5 synthetic 4x4 block (8 bytes red + 8 bytes green)
    uint8_t bc5_block[16];
    std::memset(bc5_block, 0, sizeof(bc5_block));
    bc5_block[0] = 150; // R v0
    bc5_block[8] = 220; // G v0
    std::memset(rgba_out, 0, sizeof(rgba_out));
    totk::graphics::BcDecoder::decode_bc5(bc5_block, 4, 4, rgba_out);
    assert(rgba_out[0] == 150);
    assert(rgba_out[1] == 220);
    assert(rgba_out[2] == 0);
    assert(rgba_out[3] == 255);

    // BC7 basic decode test
    uint8_t bc7_block[16];
    std::memset(bc7_block, 0, sizeof(bc7_block));
    bc7_block[0] = 0x01; // Mode 0
    std::memset(rgba_out, 0, sizeof(rgba_out));
    totk::graphics::BcDecoder::decode_bc7(bc7_block, 4, 4, rgba_out);
    // Mode 0 decodes with alpha 255
    assert(rgba_out[3] == 255);

    // 4. Texture Cache integration test
    totk::graphics::NvnTextureCache tex_cache(VK_NULL_HANDLE);
    std::memset(rgba_out, 0, sizeof(rgba_out));
    tex_cache.decompress_bc_to_rgba8(totk::graphics::TextureFormat::BC1_RGBA_UNORM, bc1_block, rgba_out, 4, 4);
    assert(rgba_out[0] >= 250);
    assert(rgba_out[3] == 255);

    std::memset(rgba_out, 0, sizeof(rgba_out));
    tex_cache.decompress_bc_to_rgba8(totk::graphics::TextureFormat::BC4_UNORM, bc4_block, rgba_out, 4, 4);
    assert(rgba_out[0] == 180);

    std::memset(rgba_out, 0, sizeof(rgba_out));
    tex_cache.decompress_bc_to_rgba8(totk::graphics::TextureFormat::BC5_UNORM, bc5_block, rgba_out, 4, 4);
    assert(rgba_out[0] == 150 && rgba_out[1] == 220);

    std::printf("  -> Compute Dispatch, Texture Views & BC Decoders (BC1-BC7): PASSED\n");
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
    test_gameframework_render_loop_10_frames();
    test_bnsh_parsing_and_fallback_pipeline();
    test_nvn_bindless_descriptor_pool();
    test_nvn_render_state_tracker();
    test_compute_views_and_bc_decoders();

    std::printf("\nAll NVN-to-Vulkan unit tests PASSED successfully!\n");
    return 0;
}
