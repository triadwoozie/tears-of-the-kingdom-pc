#include "overlay.h"
#include "imgui.h"
#include <algorithm>
#include <cstdio>

namespace totk::ui {

Overlay::Overlay() = default;
Overlay::~Overlay() = default;

void Overlay::render(double frame_time_ms, double gpu_time_ms) {
    if (!visible_) return;

    frame_history_[history_offset_] = static_cast<float>(frame_time_ms);
    history_offset_ = (history_offset_ + 1) % 120;

    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 240.0f), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                            ImGuiWindowFlags_AlwaysAutoResize |
                            ImGuiWindowFlags_NoSavedSettings |
                            ImGuiWindowFlags_NoFocusOnAppearing |
                            ImGuiWindowFlags_NoNav;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.07f, 0.09f, 0.82f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.85f, 0.95f, 0.60f));

    if (ImGui::Begin("TotK_Performance_HUD", nullptr, flags)) {
        ImGui::TextColored(ImVec4(0.0f, 0.90f, 1.0f, 1.0f), "TotK-Recomp Overlay [F1]");
        ImGui::Separator();

        float fps = (frame_time_ms > 0.0) ? static_cast<float>(1000.0 / frame_time_ms) : 0.0f;
        ImGui::Text("FPS: %.1f (%.2f ms)", fps, frame_time_ms);
        ImGui::Text("GPU Time: %.2f ms", gpu_time_ms);

        ImGui::PlotLines("##FrameGraph", frame_history_, 120, history_offset_, "Frame Time (ms)", 0.0f, 33.3f, ImVec2(310, 60));

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.7f, 0.8f, 1.0f), "Quick Controls:");
        ImGui::BulletText("Minus + Plus: Toggle Overlay");
        ImGui::BulletText("Alt + Enter: Fullscreen");
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
}

} // namespace totk::ui
