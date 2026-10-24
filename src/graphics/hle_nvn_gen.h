#pragma once

#include <cstdint>

namespace totk::graphics {

using FramePresentCallback = void (*)(uint64_t frame_index);

void set_frame_present_callback(FramePresentCallback cb);
uint64_t get_presented_frame_count();
void *nvn_get_proc_address(const char *name);

} // namespace totk::graphics
