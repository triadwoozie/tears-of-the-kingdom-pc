#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include "volk.h"

namespace totk::graphics {

struct ShaderReflection {
    std::string key;
    std::string stage;
    uint32_t workgroup_size[3] = {1, 1, 1};
    struct Binding {
        uint32_t slot;
        uint32_t binding;
        uint32_t size;
        std::string type;
    };
    std::vector<Binding> bindings;
};

class PipelineCacheManager {
public:
    PipelineCacheManager(VkDevice device);
    ~PipelineCacheManager();

    bool initialize(const std::string &cache_path);
    void save(const std::string &cache_path);

    VkShaderModule load_spirv_module(const std::string &key, const std::vector<uint8_t> &spv_data);
    VkPipeline get_or_create_graphics_pipeline(
        const std::string &vs_key,
        const std::string &fs_key,
        VkPipelineLayout layout,
        VkRenderPass render_pass
    );

    uint32_t get_cached_pipeline_count() const { return static_cast<uint32_t>(pipelines_.size()); }
    uint32_t get_loaded_shader_count() const { return static_cast<uint32_t>(modules_.size()); }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineCache pipeline_cache_ = VK_NULL_HANDLE;
    std::unordered_map<std::string, VkShaderModule> modules_;
    std::unordered_map<std::string, VkPipeline> pipelines_;
};

} // namespace totk::graphics
