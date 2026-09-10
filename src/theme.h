#pragma once
#include <imgui.h>

struct GLFWwindow;

// Central place for the application's look: one palette, one set of metrics.
// Everything in ui.cpp pulls colours from here rather than inventing its own,
// so the UI reads as a single designed surface instead of per-widget choices.
namespace theme {

// ---- Palette (cool slate; the accent is the only saturated colour) ----
constexpr ImVec4 bgViewport   = ImVec4(0.078f, 0.086f, 0.102f, 1.00f);
constexpr ImVec4 bgPanel      = ImVec4(0.086f, 0.094f, 0.110f, 1.00f);
constexpr ImVec4 bgRaised     = ImVec4(0.129f, 0.141f, 0.165f, 1.00f);
constexpr ImVec4 bgHover      = ImVec4(0.176f, 0.192f, 0.224f, 1.00f);
constexpr ImVec4 bgActive     = ImVec4(0.216f, 0.235f, 0.275f, 1.00f);
constexpr ImVec4 border       = ImVec4(0.196f, 0.212f, 0.247f, 1.00f);

constexpr ImVec4 text         = ImVec4(0.871f, 0.894f, 0.925f, 1.00f);
constexpr ImVec4 textDim      = ImVec4(0.529f, 0.573f, 0.635f, 1.00f);
constexpr ImVec4 textFaint    = ImVec4(0.376f, 0.408f, 0.463f, 1.00f);

constexpr ImVec4 accent       = ImVec4(0.290f, 0.553f, 0.965f, 1.00f);
constexpr ImVec4 accentHover  = ImVec4(0.376f, 0.620f, 1.000f, 1.00f);
constexpr ImVec4 accentSoft   = ImVec4(0.290f, 0.553f, 0.965f, 0.18f);

constexpr ImVec4 success      = ImVec4(0.267f, 0.686f, 0.451f, 1.00f);
constexpr ImVec4 successHover = ImVec4(0.318f, 0.769f, 0.514f, 1.00f);
constexpr ImVec4 danger       = ImVec4(0.855f, 0.353f, 0.310f, 1.00f);

// ---- Metrics ----
constexpr float toolbarHeight = 52.0f;
constexpr float statusHeight  = 30.0f;
constexpr float panelWidth    = 208.0f;
constexpr float propsWidth    = 244.0f;
constexpr float buttonHeight  = 30.0f;

// Loads the system UI font at the display's pixel density and installs the
// palette above into ImGuiStyle.  Call once, after the ImGui context exists.
void apply(GLFWwindow* window);

// A small dimmed uppercase label, used to title groups without shouting.
void sectionLabel(const char* text);

// Secondary text helper.
void dimText(const char* fmt, ...);

} // namespace theme
