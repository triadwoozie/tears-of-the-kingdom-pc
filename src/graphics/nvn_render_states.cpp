#include "graphics/nvn_render_states.h"
#include <cstdio>
#include <cstring>

namespace totk::graphics {

// NVN State Bitfields
struct NvnRawBlendState {
    uint64_t target : 3;
    uint64_t src_rgb : 7;
    uint64_t dst_rgb : 7;
    uint64_t src_a : 7;
    uint64_t dst_a : 7;
    uint64_t eq_rgb : 3;
    uint64_t eq_a : 3;
};

struct NvnRawDepthStencilState {
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
};

struct NvnRawPolygonState {
    uint32_t cull : 2;
    uint32_t front : 1;
    uint32_t mode : 2;
    uint32_t offset_en : 3;
};

struct NvnRawColorState {
    uint32_t blend_en : 8;
    uint32_t logic_op : 4;
    uint32_t alpha_func : 4;
};

struct NvnRawMultisampleState {
    uint8_t enable;
    uint8_t samples;
    uint8_t a2c;
    uint8_t a2c_dither;
    uint8_t raster_samples;
    uint8_t cov_mod;
    uint8_t cov2color_en;
    uint8_t cov2color_out;
    uint8_t sl_en;
    uint8_t sl_grid;
};

// Conversions
static VkBlendFactor nvn_to_vk_blend_factor(uint32_t f) {
    switch (f) {
        case 0x1: return VK_BLEND_FACTOR_ZERO;
        case 0x2: return VK_BLEND_FACTOR_ONE;
        case 0x3: return VK_BLEND_FACTOR_SRC_COLOR;
        case 0x4: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case 0x5: return VK_BLEND_FACTOR_SRC_ALPHA;
        case 0x6: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case 0x7: return VK_BLEND_FACTOR_DST_ALPHA;
        case 0x8: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case 0x9: return VK_BLEND_FACTOR_DST_COLOR;
        case 0xA: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case 0xB: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        case 0x10: return VK_BLEND_FACTOR_SRC1_COLOR;
        case 0x11: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
        case 0x12: return VK_BLEND_FACTOR_SRC1_ALPHA;
        case 0x13: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
        case 0x61: return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case 0x62: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case 0x63: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case 0x64: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        default: return VK_BLEND_FACTOR_ONE;
    }
}

static VkBlendOp nvn_to_vk_blend_op(uint32_t eq) {
    switch (eq) {
        case 0x1: return VK_BLEND_OP_ADD;
        case 0x2: return VK_BLEND_OP_SUBTRACT;
        case 0x3: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case 0x4: return VK_BLEND_OP_MIN;
        case 0x5: return VK_BLEND_OP_MAX;
        default: return VK_BLEND_OP_ADD;
    }
}

static VkCompareOp nvn_to_vk_compare_op(uint32_t df) {
    switch (df) {
        case 0x1: return VK_COMPARE_OP_NEVER;
        case 0x2: return VK_COMPARE_OP_LESS;
        case 0x3: return VK_COMPARE_OP_EQUAL;
        case 0x4: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case 0x5: return VK_COMPARE_OP_GREATER;
        case 0x6: return VK_COMPARE_OP_NOT_EQUAL;
        case 0x7: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case 0x8: return VK_COMPARE_OP_ALWAYS;
        default: return VK_COMPARE_OP_LESS;
    }
}

static VkCullModeFlags nvn_to_vk_cull_mode(uint32_t cull) {
    switch (cull) {
        case 0x0: return VK_CULL_MODE_NONE;
        case 0x1: return VK_CULL_MODE_FRONT_BIT;
        case 0x2: return VK_CULL_MODE_BACK_BIT;
        case 0x3: return VK_CULL_MODE_FRONT_AND_BACK;
        default: return VK_CULL_MODE_NONE;
    }
}

static VkFrontFace nvn_to_vk_front_face(uint32_t front) {
    return (front == 0) ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

static VkPolygonMode nvn_to_vk_polygon_mode(uint32_t mode) {
    switch (mode) {
        case 0x0: return VK_POLYGON_MODE_POINT;
        case 0x1: return VK_POLYGON_MODE_LINE;
        case 0x2: return VK_POLYGON_MODE_FILL;
        default: return VK_POLYGON_MODE_FILL;
    }
}

NvnRenderStateManager &NvnRenderStateManager::instance() {
    static NvnRenderStateManager s_instance;
    return s_instance;
}

NvnRenderStateManager::NvnRenderStateManager() {
    reset();
}

void NvnRenderStateManager::reset() {
    for (int i = 0; i < 8; ++i) {
        blend_targets_[i] = NvnBlendTargetState{};
    }
    depth_stencil_ = NvnDepthStencilStateInternal{};
    rasterizer_ = NvnRasterizerStateInternal{};
    multisample_ = NvnMultisampleStateInternal{};

    for (int i = 0; i < 16; ++i) {
        vertex_attribs_[i] = NvnVertexAttribInternal{};
        vertex_streams_[i] = NvnVertexStreamInternal{};
    }

    dirty_depth_stencil_ = true;
    dirty_rasterizer_ = true;
    dirty_blend_ = true;

    bind_blend_count_ = 0;
    bind_depth_stencil_count_ = 0;
    bind_polygon_count_ = 0;
    bind_channel_mask_count_ = 0;
    bind_multisample_count_ = 0;
}

void NvnRenderStateManager::bind_blend_state(const void *guest_state_ptr) {
    if (!guest_state_ptr) return;
    NvnRawBlendState raw{};
    std::memcpy(&raw, guest_state_ptr, sizeof(raw));

    uint32_t target = static_cast<uint32_t>(raw.target);
    if (target < 8) {
        blend_targets_[target].src_color_factor = nvn_to_vk_blend_factor(static_cast<uint32_t>(raw.src_rgb));
        blend_targets_[target].dst_color_factor = nvn_to_vk_blend_factor(static_cast<uint32_t>(raw.dst_rgb));
        blend_targets_[target].color_blend_op = nvn_to_vk_blend_op(static_cast<uint32_t>(raw.eq_rgb));
        blend_targets_[target].src_alpha_factor = nvn_to_vk_blend_factor(static_cast<uint32_t>(raw.src_a));
        blend_targets_[target].dst_alpha_factor = nvn_to_vk_blend_factor(static_cast<uint32_t>(raw.dst_a));
        blend_targets_[target].alpha_blend_op = nvn_to_vk_blend_op(static_cast<uint32_t>(raw.eq_a));
        dirty_blend_ = true;
    }
    bind_blend_count_++;
}

void NvnRenderStateManager::bind_channel_mask_state(const void *guest_state_ptr) {
    if (!guest_state_ptr) return;
    uint32_t mask = 0;
    std::memcpy(&mask, guest_state_ptr, sizeof(mask));

    for (uint32_t i = 0; i < 8; ++i) {
        VkColorComponentFlags target_mask = 0;
        uint32_t nibble = (mask >> (i * 4)) & 0xF;
        if (nibble & 0x1) target_mask |= VK_COLOR_COMPONENT_R_BIT;
        if (nibble & 0x2) target_mask |= VK_COLOR_COMPONENT_G_BIT;
        if (nibble & 0x4) target_mask |= VK_COLOR_COMPONENT_B_BIT;
        if (nibble & 0x8) target_mask |= VK_COLOR_COMPONENT_A_BIT;
        blend_targets_[i].color_write_mask = target_mask;
    }
    dirty_blend_ = true;
    bind_channel_mask_count_++;
}

void NvnRenderStateManager::bind_depth_stencil_state(const void *guest_state_ptr) {
    if (!guest_state_ptr) return;
    NvnRawDepthStencilState raw{};
    std::memcpy(&raw, guest_state_ptr, sizeof(raw));

    depth_stencil_.depth_test_enable = (raw.dt != 0);
    depth_stencil_.depth_write_enable = (raw.dw != 0);
    depth_stencil_.depth_compare_op = nvn_to_vk_compare_op(static_cast<uint32_t>(raw.df));
    depth_stencil_.stencil_test_enable = (raw.st != 0);

    dirty_depth_stencil_ = true;
    bind_depth_stencil_count_++;
}

void NvnRenderStateManager::bind_polygon_state(const void *guest_state_ptr) {
    if (!guest_state_ptr) return;
    NvnRawPolygonState raw{};
    std::memcpy(&raw, guest_state_ptr, sizeof(raw));

    rasterizer_.cull_mode = nvn_to_vk_cull_mode(raw.cull);
    rasterizer_.front_face = nvn_to_vk_front_face(raw.front);
    rasterizer_.polygon_mode = nvn_to_vk_polygon_mode(raw.mode);
    rasterizer_.polygon_offset_enable = (raw.offset_en != 0);

    dirty_rasterizer_ = true;
    bind_polygon_count_++;
}

void NvnRenderStateManager::bind_multisample_state(const void *guest_state_ptr) {
    if (!guest_state_ptr) return;
    NvnRawMultisampleState raw{};
    std::memcpy(&raw, guest_state_ptr, sizeof(raw));

    multisample_.sample_shading_enable = (raw.enable != 0);
    multisample_.alpha_to_coverage_enable = (raw.a2c != 0);
    if (raw.samples == 2) multisample_.rasterization_samples = VK_SAMPLE_COUNT_2_BIT;
    else if (raw.samples == 4) multisample_.rasterization_samples = VK_SAMPLE_COUNT_4_BIT;
    else if (raw.samples == 8) multisample_.rasterization_samples = VK_SAMPLE_COUNT_8_BIT;
    else multisample_.rasterization_samples = VK_SAMPLE_COUNT_1_BIT;

    bind_multisample_count_++;
}

void NvnRenderStateManager::bind_color_state(const void *guest_state_ptr) {
    if (!guest_state_ptr) return;
    NvnRawColorState raw{};
    std::memcpy(&raw, guest_state_ptr, sizeof(raw));

    for (uint32_t i = 0; i < 8; ++i) {
        blend_targets_[i].blend_enable = ((raw.blend_en & (1u << i)) != 0);
    }
    dirty_blend_ = true;
}

void NvnRenderStateManager::apply_dynamic_states(VkCommandBuffer cmd) {
    if (cmd == VK_NULL_HANDLE) return;

    if (vkCmdSetDepthTestEnable) {
        vkCmdSetDepthTestEnable(cmd, depth_stencil_.depth_test_enable ? VK_TRUE : VK_FALSE);
    }
    if (vkCmdSetDepthWriteEnable) {
        vkCmdSetDepthWriteEnable(cmd, depth_stencil_.depth_write_enable ? VK_TRUE : VK_FALSE);
    }
    if (vkCmdSetDepthCompareOp) {
        vkCmdSetDepthCompareOp(cmd, depth_stencil_.depth_compare_op);
    }
    if (vkCmdSetCullMode) {
        vkCmdSetCullMode(cmd, rasterizer_.cull_mode);
    }
    if (vkCmdSetFrontFace) {
        vkCmdSetFrontFace(cmd, rasterizer_.front_face);
    }
    if (vkCmdSetStencilTestEnable) {
        vkCmdSetStencilTestEnable(cmd, depth_stencil_.stencil_test_enable ? VK_TRUE : VK_FALSE);
    }

    dirty_depth_stencil_ = false;
    dirty_rasterizer_ = false;
}

uint64_t NvnRenderStateManager::compute_hash() const {
    // 64-bit FNV-1a Hash
    uint64_t hash = 14695981039346656037ULL;
    auto hash_combine = [&hash](const void *data, size_t size) {
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data);
        for (size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ULL;
        }
    };

    hash_combine(blend_targets_, sizeof(blend_targets_));
    hash_combine(&depth_stencil_, sizeof(depth_stencil_));
    hash_combine(&rasterizer_, sizeof(rasterizer_));
    hash_combine(&multisample_, sizeof(multisample_));
    hash_combine(vertex_attribs_, sizeof(vertex_attribs_));
    hash_combine(vertex_streams_, sizeof(vertex_streams_));

    return hash;
}

void NvnRenderStateManager::set_depth_test(bool enable, bool write, VkCompareOp op) {
    depth_stencil_.depth_test_enable = enable;
    depth_stencil_.depth_write_enable = write;
    depth_stencil_.depth_compare_op = op;
    dirty_depth_stencil_ = true;
}

void NvnRenderStateManager::set_cull_mode(VkCullModeFlags cull, VkFrontFace front) {
    rasterizer_.cull_mode = cull;
    rasterizer_.front_face = front;
    dirty_rasterizer_ = true;
}

void NvnRenderStateManager::set_blend_target(uint32_t target, bool enable,
                                             VkBlendFactor src_c, VkBlendFactor dst_c, VkBlendOp op_c,
                                             VkBlendFactor src_a, VkBlendFactor dst_a, VkBlendOp op_a) {
    if (target < 8) {
        blend_targets_[target].blend_enable = enable;
        blend_targets_[target].src_color_factor = src_c;
        blend_targets_[target].dst_color_factor = dst_c;
        blend_targets_[target].color_blend_op = op_c;
        blend_targets_[target].src_alpha_factor = src_a;
        blend_targets_[target].dst_alpha_factor = dst_a;
        blend_targets_[target].alpha_blend_op = op_a;
        dirty_blend_ = true;
    }
}

void NvnRenderStateManager::set_channel_mask(uint32_t target, VkColorComponentFlags mask) {
    if (target < 8) {
        blend_targets_[target].color_write_mask = mask;
        dirty_blend_ = true;
    }
}

} // namespace totk::graphics
