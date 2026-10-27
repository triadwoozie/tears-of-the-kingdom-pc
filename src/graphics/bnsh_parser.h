#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include "volk.h"

namespace totk::graphics {

enum class BnshStageKind : uint32_t {
    Vertex = 0,
    Fragment = 1,
    Geometry = 2,
    TessControl = 3,
    TessEvaluation = 4,
    Compute = 5,
    Unknown = 0xFFFFFFFF
};

struct BnshStageData {
    BnshStageKind stage = BnshStageKind::Unknown;
    std::vector<uint8_t> control_bytes;
    std::vector<uint8_t> code_bytes;
    uint32_t workgroup_x = 0;
    uint32_t workgroup_y = 0;
    uint32_t workgroup_z = 0;
    uint32_t shared_memory_bytes = 0;
};

struct BnshProgram {
    uint32_t program_id = 0;
    std::vector<BnshStageData> stages;
    bool has_vertex = false;
    bool has_fragment = false;
    bool has_compute = false;
};

class BnshParser {
public:
    static bool parse(const void *data, size_t size, std::vector<BnshProgram> &out_programs);
    static bool validate_header(const void *data, size_t size);

    // Helpers to create a minimal synthetic .bnsh binary container for testing
    static std::vector<uint8_t> create_synthetic_bnsh(const std::vector<BnshProgram> &programs);

    // Embedded Fallback Passthrough SPIR-V Bytecode
    static const uint32_t *get_fallback_vertex_spirv(size_t &out_word_count);
    static const uint32_t *get_fallback_fragment_spirv(size_t &out_word_count);

    // Fallback Pipeline Creation: ensures vkCreateGraphicsPipelines succeeds
    static VkPipeline create_fallback_pipeline(
        VkDevice device,
        VkRenderPass render_pass,
        VkPipelineLayout pipeline_layout,
        uint32_t width = 1920,
        uint32_t height = 1080
    );
};

} // namespace totk::graphics
