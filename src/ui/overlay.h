#pragma once

#include <string>

namespace totk::ui {

class Overlay {
public:
    Overlay();
    ~Overlay();

    void toggle() { visible_ = !visible_; }
    bool is_visible() const { return visible_; }
    void set_visible(bool visible) { visible_ = visible; }

    void render(double frame_time_ms, double gpu_time_ms);
    void set_guest_thread_count(int count) { guest_thread_count_ = count; }
    void set_last_boot_symbol(const char *symbol) { last_boot_symbol_ = symbol ? symbol : ""; }

private:
    bool visible_ = false;
    float frame_history_[120] = {};
    int history_offset_ = 0;
    int guest_thread_count_ = 24;
    std::string last_boot_symbol_ = "sead::GameFrameworkNx::mainLoop";
};

} // namespace totk::ui
