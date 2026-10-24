#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include "volk.h"

#include <cstdint>
#include <unordered_map>

namespace totk::graphics {

enum class ConditionalRenderMode {
    RenderIfEqual = 0,
    RenderIfNotEqual = 1
};

struct CounterQuery {
    uint64_t counter_address = 0;
    uint64_t sample_value = 0;
    bool is_active = false;
};

class NvnConditionalRenderManager {
public:
    explicit NvnConditionalRenderManager(bool supports_ext = false);
    ~NvnConditionalRenderManager();

    void set_conditional_rendering_supported(bool supported) { has_ext_ = supported; }

    void begin_conditional_render(VkCommandBuffer cmd, uint64_t counter_addr, ConditionalRenderMode mode);
    void end_conditional_render(VkCommandBuffer cmd);

    void report_counter(uint64_t counter_addr, uint64_t value);
    bool should_execute_draw(uint64_t counter_addr, ConditionalRenderMode mode) const;

    bool is_in_conditional_block() const { return in_conditional_block_; }

private:
    bool has_ext_ = false;
    bool in_conditional_block_ = false;
    uint64_t active_counter_addr_ = 0;
    ConditionalRenderMode active_mode_ = ConditionalRenderMode::RenderIfEqual;

    std::unordered_map<uint64_t, uint64_t> counter_values_;
};

} // namespace totk::graphics
