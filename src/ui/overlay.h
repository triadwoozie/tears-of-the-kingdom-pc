#pragma once

namespace totk::ui {

class Overlay {
public:
    Overlay();
    ~Overlay();

    void toggle() { visible_ = !visible_; }
    bool is_visible() const { return visible_; }
    void set_visible(bool visible) { visible_ = visible; }

    void render(double frame_time_ms, double gpu_time_ms);

private:
    bool visible_ = false;
    float frame_history_[120] = {};
    int history_offset_ = 0;
};

} // namespace totk::ui
