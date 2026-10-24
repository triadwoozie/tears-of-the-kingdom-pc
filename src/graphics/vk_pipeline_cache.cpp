#include "vk_pipeline_cache.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>

namespace totk::graphics {

PipelineCacheManager::PipelineCacheManager(VkDevice device)
    : device_(device) {}

PipelineCacheManager::~PipelineCacheManager() {
    if (device_ != VK_NULL_HANDLE) {
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
}

bool PipelineCacheManager::initialize(const std::string &cache_path) {
    if (device_ == VK_NULL_HANDLE) return true;

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
    if (!pipeline_cache_ || device_ == VK_NULL_HANDLE) return;

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

    if (device_ == VK_NULL_HANDLE) {
        // Mock offline validation mode
        modules_[key] = reinterpret_cast<VkShaderModule>(0x1);
        compiled_shaders_.fetch_add(1);
        return reinterpret_cast<VkShaderModule>(0x1);
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
    compiled_shaders_.fetch_add(1);
    return module;
}

VkShaderModule PipelineCacheManager::get_module(const std::string &key) const {
    auto it = modules_.find(key);
    return (it != modules_.end()) ? it->second : VK_NULL_HANDLE;
}

bool PipelineCacheManager::start_async_prewarm(const std::string &archive_path, const std::string &index_path) {
    std::ifstream idx_file(index_path);
    if (!idx_file.is_open()) return false;

    // Quick parse of index records
    std::vector<ShaderArchiveEntry> entries;
    std::string line;
    ShaderArchiveEntry cur;
    while (std::getline(idx_file, line)) {
        if (line.find("\"key\":") != std::string::npos) {
            size_t s = line.find('"', line.find(':')) + 1;
            size_t e = line.find('"', s);
            cur.key = line.substr(s, e - s);
        } else if (line.find("\"offset\":") != std::string::npos) {
            size_t s = line.find_first_of("0123456789");
            cur.offset = std::stoull(line.substr(s));
        } else if (line.find("\"size\":") != std::string::npos) {
            size_t s = line.find_first_of("0123456789");
            cur.size = std::stoull(line.substr(s));
            if (!cur.key.empty() && cur.size > 0) {
                entries.push_back(cur);
                cur = {};
            }
        }
    }

    total_shaders_.store(static_cast<uint32_t>(entries.size()));
    compiled_shaders_.store(0);
    is_warming_.store(true);

    std::thread([this, archive_path, entries = std::move(entries)]() {
        std::ifstream bin_file(archive_path, std::ios::binary);
        if (bin_file.is_open()) {
            for (const auto &entry : entries) {
                std::vector<uint8_t> spv(entry.size);
                bin_file.seekg(entry.offset);
                bin_file.read(reinterpret_cast<char *>(spv.data()), entry.size);
                load_spirv_module(entry.key, spv);
            }
        }
        is_warming_.store(false);
    }).detach();

    return true;
}

void PipelineCacheManager::wait_prewarm_completion() {
    while (is_warming_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

} // namespace totk::graphics
