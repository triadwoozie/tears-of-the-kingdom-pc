#include "graphics/nvn_uniform_snapshot.h"
#include "graphics/nvn_buffer_cache.h"
#include "graphics/nvn_conditional_draw.h"
#include "graphics/vk_pipeline_cache.h"
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

int main() {
    std::printf("==================================================\n");
    std::printf("  NVN-to-Vulkan 1.3 Subsystems Unit Tests\n");
    std::printf("==================================================\n");

    test_uniform_snapshot_ring();
    test_buffer_cache_tracking();
    test_conditional_rendering();
    test_pipeline_cache_progress();

    std::printf("\nAll NVN-to-Vulkan unit tests PASSED successfully!\n");
    return 0;
}
