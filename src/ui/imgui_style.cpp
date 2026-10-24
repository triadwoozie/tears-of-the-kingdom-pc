#include "imgui_style.h"

namespace totk::ui {

void apply_modern_dark_theme() {
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;

    // Window & frame geometry
    style.WindowRounding    = 8.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 6.0f;
    style.PopupRounding     = 6.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding      = 6.0f;
    style.TabRounding       = 6.0f;

    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;

    style.WindowPadding     = ImVec2(16.0f, 16.0f);
    style.FramePadding      = ImVec2(10.0f, 6.0f);
    style.ItemSpacing       = ImVec2(12.0f, 8.0f);
    style.ItemInnerSpacing  = ImVec2(8.0f, 6.0f);
    style.ScrollbarSize     = 14.0f;

    // Palette: Sleek, high-contrast modern dark with electric accents
    // Backgrounds
    colors[ImGuiCol_WindowBg]             = ImVec4(0.08f, 0.08f, 0.10f, 0.96f);
    colors[ImGuiCol_ChildBg]              = ImVec4(0.11f, 0.11f, 0.14f, 1.00f);
    colors[ImGuiCol_PopupBg]              = ImVec4(0.10f, 0.10f, 0.12f, 0.98f);
    colors[ImGuiCol_Border]               = ImVec4(0.18f, 0.20f, 0.25f, 0.80f);
    colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Text
    colors[ImGuiCol_Text]                 = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
    colors[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.52f, 0.58f, 1.00f);

    // Headers & tabs
    colors[ImGuiCol_Header]               = ImVec4(0.16f, 0.18f, 0.24f, 1.00f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.20f, 0.24f, 0.32f, 1.00f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.24f, 0.28f, 0.40f, 1.00f);

    colors[ImGuiCol_Tab]                  = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
    colors[ImGuiCol_TabHovered]           = ImVec4(0.20f, 0.40f, 0.80f, 0.80f);
    colors[ImGuiCol_TabActive]            = ImVec4(0.15f, 0.35f, 0.75f, 1.00f);
    colors[ImGuiCol_TabUnfocused]         = ImVec4(0.09f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.13f, 0.15f, 0.20f, 1.00f);

    // Buttons
    colors[ImGuiCol_Button]               = ImVec4(0.15f, 0.17f, 0.23f, 1.00f);
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.22f, 0.38f, 0.70f, 1.00f);
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.18f, 0.48f, 0.90f, 1.00f);

    // Frame inputs (Text fields, sliders)
    colors[ImGuiCol_FrameBg]              = ImVec4(0.13f, 0.14f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.17f, 0.19f, 0.26f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.20f, 0.23f, 0.32f, 1.00f);

    // Sliders & checks
    colors[ImGuiCol_CheckMark]            = ImVec4(0.00f, 0.85f, 0.95f, 1.00f);
    colors[ImGuiCol_SliderGrab]           = ImVec4(0.18f, 0.48f, 0.90f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.00f, 0.85f, 0.95f, 1.00f);

    // Title & modal
    colors[ImGuiCol_TitleBg]              = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.06f, 0.06f, 0.08f, 0.60f);

    // Scrollbar
    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.08f, 0.08f, 0.10f, 0.40f);
    colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.20f, 0.22f, 0.28f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.28f, 0.30f, 0.38f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.35f, 0.38f, 0.48f, 1.00f);

    // Separators
    colors[ImGuiCol_Separator]            = ImVec4(0.18f, 0.20f, 0.25f, 0.80f);
    colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.25f, 0.45f, 0.80f, 0.80f);
    colors[ImGuiCol_SeparatorActive]      = ImVec4(0.30f, 0.55f, 0.95f, 1.00f);
}

} // namespace totk::ui
