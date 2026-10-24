#include "vk_pipeline_cache.h"
#include <cstdio>
#include <fstream>

namespace totk::graphics {

PipelineCacheManager::PipelineCacheManager(VkDevice device)
    : device_(device) {}

PipelineCacheManager::~PipelineCacheManager() {
    for (auto &[_, pipeline] : pipelines_) {
        vkDestroyPipeline(device_, pipeline, nullptr);
    }
    for (auto &[_, module] : modules_) {
        vkDestroyShaderModule(device_, module, nullptr);
    }
    if (pipeline_cache_) {
        vkDestroyPipelineCache(device_, pipeline_cache_, nullptr);
    }
}

bool PipelineCacheManager::initialize(const std::string &cache_path) {
    std::vector<char> initial_data;
    std::ifstream file(cache_path, std::ios::binary | std::ios::ate);
    if (file.is_open()) {
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);
        initial_data.resize(size);
        file.read(initial_data.data(), size);
    }

    VkPipelineCacheCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    create_info.initialDataSize = initial_data.size();
    create_info.pInitialData = initial_data.empty() ? nullptr : initial_data.data();

    return vkCreatePipelineCache(device_, &create_info, nullptr, &pipeline_cache_) == VK_SUCCESS;
}

void PipelineCacheManager::save(const std::string &cache_path) {
    if (!pipeline_cache_) return;

    size_t size = 0;
    vkGetPipelineCacheData(device_, pipeline_cache_, &size, nullptr);
    if (size == 0) return;

    std::vector<char> data(size);
    if (vkGetPipelineCacheData(device_, pipeline_cache_, &size, data.data()) == VK_SUCCESS) {
        std::ofstream file(cache_path, std::ios::binary);
        if (file.is_open()) {
            file.write(data.data(), size);
        }
    }
}

VkShaderModule PipelineCacheManager::load_spirv_module(const std::string &key, const std::vector<uint8_t> &spv_data) {
    auto it = modules_.find(key);
    if (it != modules_.end()) return it->second;

    if (spv_data.size() < 20 || (spv_data.size() % 4 != 0)) {
        std::fprintf(stderr, "[PipelineCache] Invalid SPIR-V size for key %s\n", key.c_str());
        return VK_NULL_HANDLE;
    }

    // Validate SPIR-V magic number (0x07230203)
    uint32_t magic = *reinterpret_cast<const uint32_t *>(spv_data.data());
    if (magic != 0x07230203) {
        std::fprintf(stderr, "[PipelineCache] Invalid SPIR-V magic header for key %s\n", key.c_str());
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = spv_data.size();
    create_info.pCode = reinterpret_cast<const uint32_t *>(spv_data.data());

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device_, &create_info, nullptr, &module) != VK_SUCCESS) {
        std::fprintf(stderr, "[PipelineCache] vkCreateShaderModule failed for key %s\n", key.c_str());
        return VK_NULL_HANDLE;
    }

    modules_[key] = module;
    return module;
}

} // namespace totk::graphics
