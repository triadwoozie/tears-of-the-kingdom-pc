#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include "volk.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace totk::graphics {

struct ShaderArchiveEntry {
    std::string key;
    std::string stage;
    std::string hash;
    uint64_t offset = 0;
    uint64_t size = 0;
    std::string sha256;
};

class PipelineCacheManager {
public:
    explicit PipelineCacheManager(VkDevice device = VK_NULL_HANDLE);
    ~PipelineCacheManager();

    void set_device(VkDevice device) { device_ = device; }

    bool initialize(const std::string &cache_path);
    void save(const std::string &cache_path);

    // Asynchronous pipeline pre-warming
    bool start_async_prewarm(const std::string &archive_path, const std::string &index_path);
    void wait_prewarm_completion();

    VkShaderModule load_spirv_module(const std::string &key, const std::vector<uint8_t> &spv_data);
    VkShaderModule get_module(const std::string &key) const;

    uint32_t get_cached_pipeline_count() const { return static_cast<uint32_t>(pipelines_.size()); }
    uint32_t get_loaded_shader_count() const { return static_cast<uint32_t>(modules_.size()); }

    // Atomic progress tracking for UI
    uint32_t get_compiled_count() const { return compiled_shaders_.load(); }
    uint32_t get_total_count() const { return total_shaders_.load(); }
    bool is_warming() const { return is_warming_.load(); }
    float get_progress() const {
        uint32_t tot = total_shaders_.load();
        return tot > 0 ? static_cast<float>(compiled_shaders_.load()) / static_cast<float>(tot) : 1.0f;
    }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineCache pipeline_cache_ = VK_NULL_HANDLE;
    std::unordered_map<std::string, VkShaderModule> modules_;
    std::unordered_map<std::string, VkPipeline> pipelines_;

    std::atomic<uint32_t> compiled_shaders_{0};
    std::atomic<uint32_t> total_shaders_{0};
    std::atomic<bool> is_warming_{false};
};

} // namespace totk::graphics
