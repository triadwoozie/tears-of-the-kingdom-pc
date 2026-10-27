#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include "volk.h"

namespace totk::graphics {

struct NvnBlendTargetState {
    bool blend_enable = false;
    VkBlendFactor src_color_factor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dst_color_factor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp color_blend_op = VK_BLEND_OP_ADD;
    VkBlendFactor src_alpha_factor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dst_alpha_factor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp alpha_blend_op = VK_BLEND_OP_ADD;
    VkColorComponentFlags color_write_mask = VK_COLOR_COMPONENT_R_BIT |
                                             VK_COLOR_COMPONENT_G_BIT |
                                             VK_COLOR_COMPONENT_B_BIT |
                                             VK_COLOR_COMPONENT_A_BIT;
};

struct NvnDepthStencilStateInternal {
    bool depth_test_enable = false;
    bool depth_write_enable = true;
    VkCompareOp depth_compare_op = VK_COMPARE_OP_LESS;
    bool depth_bounds_test_enable = false;

    bool stencil_test_enable = false;
    VkStencilOpState front{};
    VkStencilOpState back{};
};

struct NvnRasterizerStateInternal {
    VkCullModeFlags cull_mode = VK_CULL_MODE_NONE;
    VkFrontFace front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPolygonMode polygon_mode = VK_POLYGON_MODE_FILL;
    bool depth_clamp_enable = false;
    bool rasterizer_discard_enable = false;
    bool polygon_offset_enable = false;
    float polygon_offset_factor = 0.0f;
    float polygon_offset_units = 0.0f;
};

struct NvnMultisampleStateInternal {
    VkSampleCountFlagBits rasterization_samples = VK_SAMPLE_COUNT_1_BIT;
    bool sample_shading_enable = false;
    bool alpha_to_coverage_enable = false;
    bool alpha_to_one_enable = false;
};

struct NvnVertexAttribInternal {
    uint32_t format = 0;
    uint32_t offset = 0;
    uint32_t stream_index = 0;
    bool is_valid = false;
};

struct NvnVertexStreamInternal {
    uint32_t stride = 0;
    uint32_t divisor = 0;
    bool is_valid = false;
};

class NvnRenderStateManager {
public:
    static NvnRenderStateManager &instance();

    void reset();

    // State Binders
    void bind_blend_state(const void *guest_state_ptr);
    void bind_channel_mask_state(const void *guest_state_ptr);
    void bind_depth_stencil_state(const void *guest_state_ptr);
    void bind_polygon_state(const void *guest_state_ptr);
    void bind_multisample_state(const void *guest_state_ptr);
    void bind_color_state(const void *guest_state_ptr);

    // Dynamic State Application (Vulkan 1.3)
    void apply_dynamic_states(VkCommandBuffer cmd);

    // Hash computation for pipeline lookup / caching
    uint64_t compute_hash() const;

    // Getters
    const NvnBlendTargetState &get_blend_target(uint32_t target) const {
        return blend_targets_[target < 8 ? target : 0];
    }
    const NvnDepthStencilStateInternal &get_depth_stencil() const { return depth_stencil_; }
    const NvnRasterizerStateInternal &get_rasterizer() const { return rasterizer_; }
    const NvnMultisampleStateInternal &get_multisample() const { return multisample_; }

    // Direct configuration helpers
    void set_depth_test(bool enable, bool write, VkCompareOp op);
    void set_cull_mode(VkCullModeFlags cull, VkFrontFace front = VK_FRONT_FACE_COUNTER_CLOCKWISE);
    void set_blend_target(uint32_t target, bool enable,
                          VkBlendFactor src_c, VkBlendFactor dst_c, VkBlendOp op_c,
                          VkBlendFactor src_a, VkBlendFactor dst_a, VkBlendOp op_a);
    void set_channel_mask(uint32_t target, VkColorComponentFlags mask);

    // Call stats
    uint64_t get_bind_blend_count() const { return bind_blend_count_; }
    uint64_t get_bind_depth_stencil_count() const { return bind_depth_stencil_count_; }
    uint64_t get_bind_polygon_count() const { return bind_polygon_count_; }
    uint64_t get_bind_channel_mask_count() const { return bind_channel_mask_count_; }
    uint64_t get_bind_multisample_count() const { return bind_multisample_count_; }

private:
    NvnRenderStateManager();
    ~NvnRenderStateManager() = default;

    NvnBlendTargetState blend_targets_[8]{};
    NvnDepthStencilStateInternal depth_stencil_{};
    NvnRasterizerStateInternal rasterizer_{};
    NvnMultisampleStateInternal multisample_{};
    NvnVertexAttribInternal vertex_attribs_[16]{};
    NvnVertexStreamInternal vertex_streams_[16]{};

    bool dirty_depth_stencil_ = true;
    bool dirty_rasterizer_ = true;
    bool dirty_blend_ = true;

    uint64_t bind_blend_count_ = 0;
    uint64_t bind_depth_stencil_count_ = 0;
    uint64_t bind_polygon_count_ = 0;
    uint64_t bind_channel_mask_count_ = 0;
    uint64_t bind_multisample_count_ = 0;
};

} // namespace totk::graphics
