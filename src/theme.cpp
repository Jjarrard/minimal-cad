#include "theme.h"

#include <GLFW/glfw3.h>
#include <cstdarg>
#include <cstdio>

namespace theme {

// Candidate UI faces, best first.  SFNS is the macOS system font; the rest are
// fallbacks so a missing font never leaves the app unreadable — ImGui falls
// back to its built-in bitmap face if every one of these misses.
static const char* kFontCandidates[] = {
    "/System/Library/Fonts/SFNS.ttf",
    "/System/Library/Fonts/HelveticaNeue.ttc",
    "/System/Library/Fonts/Helvetica.ttc",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "C:\\Windows\\Fonts\\segoeui.ttf",
};

static void loadFont(GLFWwindow* window) {
    ImGuiIO& io = ImGui::GetIO();

    // Rasterise at the display's true pixel density, then scale back down, so
    // text is sharp on a Retina panel instead of a blurry upscale.
    float xscale = 1.0f, yscale = 1.0f;
    if (window) glfwGetWindowContentScale(window, &xscale, &yscale);
    if (xscale < 1.0f) xscale = 1.0f;

    const float uiSize = 14.0f;
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = false;

    for (const char* path : kFontCandidates) {
        if (FILE* f = std::fopen(path, "rb")) {
            std::fclose(f);
            if (io.Fonts->AddFontFromFileTTF(path, uiSize * xscale, &cfg)) {
                io.FontGlobalScale = 1.0f / xscale;
                return;
            }
        }
    }
    // No system font found — keep ImGui's built-in face.
}

void apply(GLFWwindow* window) {
    loadFont(window);

    ImGuiStyle& s = ImGui::GetStyle();

    // ---- Metrics: generous hit targets, restrained rounding ----
    s.WindowPadding     = ImVec2(12, 12);
    s.FramePadding      = ImVec2(10, 6);
    s.ItemSpacing       = ImVec2(8, 7);
    s.ItemInnerSpacing  = ImVec2(6, 5);
    s.CellPadding       = ImVec2(6, 4);
    s.IndentSpacing     = 16;
    s.ScrollbarSize     = 11;
    s.GrabMinSize       = 9;

    s.WindowBorderSize  = 1;
    s.ChildBorderSize   = 1;
    s.PopupBorderSize   = 1;
    s.FrameBorderSize   = 0;

    s.WindowRounding    = 0;
    s.ChildRounding     = 5;
    s.FrameRounding     = 5;
    s.PopupRounding     = 7;
    s.ScrollbarRounding = 6;
    s.GrabRounding      = 5;
    s.TabRounding       = 5;

    s.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
    s.ButtonTextAlign   = ImVec2(0.5f, 0.5f);
    s.SeparatorTextBorderSize = 1;
    s.SeparatorTextPadding    = ImVec2(16, 4);

    // ---- Colours ----
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                  = text;
    c[ImGuiCol_TextDisabled]          = textFaint;
    c[ImGuiCol_WindowBg]              = bgPanel;
    c[ImGuiCol_ChildBg]               = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]               = bgRaised;
    c[ImGuiCol_Border]                = border;
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg]               = bgRaised;
    c[ImGuiCol_FrameBgHovered]        = bgHover;
    c[ImGuiCol_FrameBgActive]         = bgActive;

    c[ImGuiCol_TitleBg]               = bgPanel;
    c[ImGuiCol_TitleBgActive]         = bgPanel;
    c[ImGuiCol_TitleBgCollapsed]      = bgPanel;
    c[ImGuiCol_MenuBarBg]             = bgPanel;

    c[ImGuiCol_ScrollbarBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]         = bgActive;
    c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.28f, 0.31f, 0.36f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive]   = accent;

    c[ImGuiCol_CheckMark]             = accent;
    c[ImGuiCol_SliderGrab]            = accent;
    c[ImGuiCol_SliderGrabActive]      = accentHover;

    c[ImGuiCol_Button]                = bgRaised;
    c[ImGuiCol_ButtonHovered]         = bgHover;
    c[ImGuiCol_ButtonActive]          = bgActive;

    // Headers are used by CollapsingHeader / Selectable.  Keep them quiet —
    // a bright fill on every group title makes the panel read as noise.
    c[ImGuiCol_Header]                = accentSoft;
    c[ImGuiCol_HeaderHovered]         = bgHover;
    c[ImGuiCol_HeaderActive]          = bgActive;

    c[ImGuiCol_Separator]             = border;
    c[ImGuiCol_SeparatorHovered]      = accent;
    c[ImGuiCol_SeparatorActive]       = accent;

    c[ImGuiCol_ResizeGrip]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered]     = accentSoft;
    c[ImGuiCol_ResizeGripActive]      = accent;

    c[ImGuiCol_Tab]                   = bgRaised;
    c[ImGuiCol_TabHovered]            = bgHover;
    c[ImGuiCol_TabSelected]           = accent;

    c[ImGuiCol_TextSelectedBg]        = accentSoft;
    c[ImGuiCol_NavCursor]             = accent;
    c[ImGuiCol_DragDropTarget]        = accent;
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.02f, 0.03f, 0.04f, 0.55f);
}

void sectionLabel(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text, textFaint);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
}

void dimText(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, textDim);
    ImGui::TextV(fmt, args);
    ImGui::PopStyleColor();
    va_end(args);
}

} // namespace theme
