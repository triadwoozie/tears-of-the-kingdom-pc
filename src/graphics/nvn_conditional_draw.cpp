#include "nvn_conditional_draw.h"

namespace totk::graphics {

NvnConditionalRenderManager::NvnConditionalRenderManager(bool supports_ext)
    : has_ext_(supports_ext) {}

NvnConditionalRenderManager::~NvnConditionalRenderManager() = default;

void NvnConditionalRenderManager::begin_conditional_render(VkCommandBuffer cmd, uint64_t counter_addr, ConditionalRenderMode mode) {
    in_conditional_block_ = true;
    active_counter_addr_ = counter_addr;
    active_mode_ = mode;

    if (has_ext_ && cmd != VK_NULL_HANDLE) {
        // vkCmdBeginConditionalRenderingEXT call when extension available
    }
}

void NvnConditionalRenderManager::end_conditional_render(VkCommandBuffer cmd) {
    if (has_ext_ && cmd != VK_NULL_HANDLE) {
        // vkCmdEndConditionalRenderingEXT call when extension available
    }
    in_conditional_block_ = false;
    active_counter_addr_ = 0;
}

void NvnConditionalRenderManager::report_counter(uint64_t counter_addr, uint64_t value) {
    counter_values_[counter_addr] = value;
}

bool NvnConditionalRenderManager::should_execute_draw(uint64_t counter_addr, ConditionalRenderMode mode) const {
    if (!in_conditional_block_) return true;

    auto it = counter_values_.find(counter_addr);
    uint64_t val = (it != counter_values_.end()) ? it->second : 0;

    if (mode == ConditionalRenderMode::RenderIfEqual) {
        return val != 0;
    } else {
        return val == 0;
    }
}

} // namespace totk::graphics
