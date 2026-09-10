#include "ui.h"
#include "input.h"
#include "extrude.h"
#include "undo.h"
#include "platform_dialogs.h"
#include "save_load.h"
#include "theme.h"
#include "tool_move.h"

#include <imgui.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>

using namespace cad;

static void openExportStlDialog() {
    app.showExportStlDialog = true;
    app.focusExportStlPath = true;
}

static void drawExportStlDialog() {
    if (app.showExportStlDialog) {
#ifdef __APPLE__
        char exportPath[sizeof(app.exportStlPathBuf)] = "";
        if (showNativeStlSaveDialog(app.exportStlPathBuf, exportPath, sizeof(exportPath))) {
            std::snprintf(app.exportStlPathBuf, sizeof(app.exportStlPathBuf), "%s", exportPath);
            if (exportStl(exportPath)) {
                app.statusText = std::string("Exported ") + exportPath;
            }
        } else {
            app.statusText = "STL export cancelled";
        }
        app.showExportStlDialog = false;
        return;
#else
        ImGui::OpenPopup("Export STL");
        app.showExportStlDialog = false;
#endif
    }

    ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Export STL", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Export visible solids as an ASCII STL mesh.");
        ImGui::Spacing();
        ImGui::Text("Path");
        ImGui::SetNextItemWidth(460.0f);
        if (app.focusExportStlPath) {
            ImGui::SetKeyboardFocusHere();
            app.focusExportStlPath = false;
        }
        bool submit = ImGui::InputText("##export-stl-path", app.exportStlPathBuf, sizeof(app.exportStlPathBuf),
            ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::Spacing();
        if (ImGui::Button("Export", ImVec2(100, 0)) || submit) {
            if (app.exportStlPathBuf[0] == '\0') {
                app.statusText = "STL export failed: enter a file path";
                app.focusExportStlPath = true;
            } else if (exportStl(app.exportStlPathBuf)) {
                app.statusText = std::string("Exported ") + app.exportStlPathBuf;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            app.statusText = "STL export cancelled";
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// ---- Dimension Input Bar ----
void drawDimensionInput() {
    if (!app.dimInput.active || !app.sketchDrawing) return;

    float barW = 340;
    float barH = 50;
    float barX = ((float)app.windowWidth - barW) * 0.5f;
    float barY = (float)app.windowHeight - 90;

    ImGui::SetNextWindowPos(ImVec2(barX, barY));
    ImGui::SetNextWindowSize(ImVec2(barW, barH));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.15f, 0.95f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::Begin("##DimInput", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoCollapse);

    if (app.currentTool == Tool::SketchCircle) {
        ImGui::Text("Radius:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        if (app.dimInput.focusLength) {
            ImGui::SetKeyboardFocusHere();
            app.dimInput.focusLength = false;
        }
        if (ImGui::InputText("mm##rad", app.dimInput.lengthBuf, sizeof(app.dimInput.lengthBuf),
                ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
            app.dimInput.length = (float)std::atof(app.dimInput.lengthBuf);
            app.dimInput.lengthSet = app.dimInput.length > 0;
            if (app.dimInput.lengthSet) {
                app.sketch.addCircle(app.sketchDrawStart, app.dimInput.length);
                app.sketchDrawing = false;
                app.dimInput.reset();
            }
        }
        if (std::strlen(app.dimInput.lengthBuf) > 0) {
            app.dimInput.length = (float)std::atof(app.dimInput.lengthBuf);
            app.dimInput.lengthSet = app.dimInput.length > 0;
        }
    } else if (app.currentTool == Tool::SketchRect) {
        // Build hint strings from live mouse-derived width/height
        char wHint[32] = "";
        char hHint[32] = "";
        if (std::abs(app.dimInput.liveLength) > 0.001f)
            std::snprintf(wHint, sizeof(wHint), "%.2f", app.dimInput.liveLength);
        if (std::abs(app.dimInput.liveAngle) > 0.001f)
            std::snprintf(hHint, sizeof(hHint), "%.2f", app.dimInput.liveAngle);

        ImGui::Text("W:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        if (app.dimInput.focusLength) {
            ImGui::SetKeyboardFocusHere();
            app.dimInput.focusLength = false;
        }
        bool enterW = ImGui::InputTextWithHint("mm##w", wHint, app.dimInput.lengthBuf, sizeof(app.dimInput.lengthBuf),
            ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);
        // Tab from W to H
        if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Tab)) {
            app.dimInput.focusAngle = true;
        }
        ImGui::SameLine();
        ImGui::Text("H:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        if (app.dimInput.focusAngle) {
            ImGui::SetKeyboardFocusHere();
            app.dimInput.focusAngle = false;
        }
        bool enterH = ImGui::InputTextWithHint("mm##h", hHint, app.dimInput.angleBuf, sizeof(app.dimInput.angleBuf),
            ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);

        // Parse typed values
        if (std::strlen(app.dimInput.lengthBuf) > 0) {
            app.dimInput.length = (float)std::atof(app.dimInput.lengthBuf);
            app.dimInput.lengthSet = true;
        } else {
            app.dimInput.lengthSet = false;
        }
        if (std::strlen(app.dimInput.angleBuf) > 0) {
            app.dimInput.angle = (float)std::atof(app.dimInput.angleBuf);
            app.dimInput.angleSet = true;
        } else {
            app.dimInput.angleSet = false;
        }

        // Enter commits: use typed values where available, mouse-derived for the rest
        if (enterW || enterH) {
            float w = app.dimInput.lengthSet ? app.dimInput.length : app.dimInput.liveLength;
            float h = app.dimInput.angleSet ? app.dimInput.angle : app.dimInput.liveAngle;
            if (std::abs(w) > 0.001f && std::abs(h) > 0.001f) {
                Vec2 end = app.sketchDrawStart + Vec2{w, h};
                app.sketch.addRectangle(app.sketchDrawStart, end);
                app.sketchDrawing = false;
                app.dimInput.reset();
            }
        }
    } else {
        // Line tool — Length + Angle
        // Build hint strings from live mouse values
        char lenHint[32] = "";
        char angHint[32] = "";
        if (app.dimInput.liveLength > 0)
            std::snprintf(lenHint, sizeof(lenHint), "%.2f", app.dimInput.liveLength);
        std::snprintf(angHint, sizeof(angHint), "%.1f", app.dimInput.liveAngle);

        ImGui::Text("L:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        if (app.dimInput.focusLength) {
            ImGui::SetKeyboardFocusHere();
            app.dimInput.focusLength = false;
        }
        bool enterL = ImGui::InputTextWithHint("mm##len", lenHint, app.dimInput.lengthBuf, sizeof(app.dimInput.lengthBuf),
            ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::SameLine();
        ImGui::Text("Angle:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        if (app.dimInput.focusAngle) {
            ImGui::SetKeyboardFocusHere();
            app.dimInput.focusAngle = false;
        }
        bool enterA = ImGui::InputTextWithHint("deg##ang", angHint, app.dimInput.angleBuf, sizeof(app.dimInput.angleBuf),
            ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);

        // Parse user-typed values
        if (std::strlen(app.dimInput.lengthBuf) > 0) {
            app.dimInput.length = (float)std::atof(app.dimInput.lengthBuf);
            app.dimInput.lengthSet = app.dimInput.length > 0;
        } else {
            app.dimInput.lengthSet = false;
        }
        if (std::strlen(app.dimInput.angleBuf) > 0) {
            app.dimInput.angle = (float)std::atof(app.dimInput.angleBuf);
            app.dimInput.angleSet = true;
        } else {
            app.dimInput.angleSet = false;
        }

        if (enterL || enterA) {
            float len = app.dimInput.lengthSet ? app.dimInput.length : app.dimInput.liveLength;
            float ang = app.dimInput.angleSet ? app.dimInput.angle : app.dimInput.liveAngle;
            if (len > 0) {
                Vec2 end = computeEndpoint(app.sketchDrawStart, len, ang);
                app.sketch.addLine(app.sketchDrawStart, end);
                app.sketchDrawStart = end;
                app.dimInput.activate();
            }
        }
    }

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ---- Plane Picker (label overlay for origin cube) ----
void drawPlanePickerUI() {
    if (app.planePick != PlanePickMode::WaitingForPlane) return;

    // Show a small hint at bottom
    float barW = 320, barH = 32;
    float barX = ((float)app.windowWidth - barW) * 0.5f;
    float barY = (float)app.windowHeight - 50;
    ImGui::SetNextWindowPos(ImVec2(barX, barY));
    ImGui::SetNextWindowSize(ImVec2(barW, barH));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.13f, 0.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::Begin("##PlaneHint", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.8f, 1.0f), "Click a face on the origin cube, or a solid face");
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // Draw face labels near the origin cube faces via ImGui foreground draw list
    const float cubeSize = 3.0f;
    const char* labels[3] = {"Top", "Front", "Side"};
    Vec3 labelPositions[3] = {
        {cubeSize * 0.5f, 0.15f, cubeSize * 0.5f},        // Top face center
        {cubeSize * 0.5f, cubeSize * 0.5f, -0.15f},       // Front face center
        {-0.15f, cubeSize * 0.5f, cubeSize * 0.5f},       // Side face center
    };
    ImU32 labelColors[3] = {
        IM_COL32(80, 200, 80, 220),
        IM_COL32(80, 80, 220, 220),
        IM_COL32(220, 80, 80, 220),
    };

    Mat4 vp = app.camera.projMatrix() * app.camera.viewMatrix();
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    for (int i = 0; i < 3; i++) {
        Vec4 clip = vp * Vec4(labelPositions[i], 1.0f);
        if (clip.w <= 0) continue;
        float sx = (clip.x / clip.w * 0.5f + 0.5f) * app.windowWidth;
        float sy = (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * app.windowHeight;

        ImU32 col = (app.hoveredOriginPlane == i) ? IM_COL32(255, 255, 255, 255) : labelColors[i];
        ImVec2 textSize = ImGui::CalcTextSize(labels[i]);
        dl->AddText(ImVec2(sx - textSize.x * 0.5f, sy - textSize.y * 0.5f), col, labels[i]);
    }
}

// ---- Main UI ----
// ---- Properties panel (right side) ----
// Area of a planar polygon, via the magnitude of its Newell vector.
static float polygonArea(const std::vector<Vec3>& loop) {
    if (loop.size() < 3) return 0.0f;
    Vec3 n{0, 0, 0};
    for (size_t i = 0; i < loop.size(); i++) {
        const Vec3& a = loop[i];
        const Vec3& b = loop[(i + 1) % loop.size()];
        n += a.cross(b);
    }
    return n.length() * 0.5f;
}

static void drawPropertiesPanel() {
    ImGui::SetNextWindowPos(ImVec2((float)app.windowWidth - theme::propsWidth, theme::toolbarHeight));
    ImGui::SetNextWindowSize(ImVec2(theme::propsWidth,
        (float)app.windowHeight - theme::toolbarHeight - theme::statusHeight));
    ImGui::Begin("##Properties", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse);

if (app.currentTool == Tool::Extrude) {
    ImGui::Text("Extrude");
    ImGui::Separator();

    // Direction combo
    const char* dirLabels[] = {"One Side", "Symmetric", "Two Sides"};
    int dirIdx = (int)app.extrudeDir;
    if (ImGui::Combo("Direction", &dirIdx, dirLabels, 3)) {
        app.extrudeDir = (ExtrudeDirection)dirIdx;
        updateExtrudePreview();
    }

    // Boolean op combo
    const char* boolLabels[] = {"New Body", "Join", "Cut", "Intersect"};
    int boolIdx = (int)app.extrudeBoolOp;
    if (ImGui::Combo("Operation", &boolIdx, boolLabels, 4)) {
        app.extrudeBoolOp = (ExtrudeBoolOp)boolIdx;
        app.extrudeBoolManual = true;
    }

    if (ImGui::InputFloat("Distance##ext", &app.extrudeDistance, 1.0f, 10.0f, "%.1f mm")) {
        updateExtrudePreview();
    }
    if (app.extrudeDir == ExtrudeDirection::TwoSides) {
        if (ImGui::InputFloat("Distance 2##ext2", &app.extrudeDistance2, 1.0f, 10.0f, "%.1f mm")) {
            updateExtrudePreview();
        }
    }

    if (!app.sketch.entities.empty()) {
        auto profiles = app.sketch.extractProfiles();
        ImGui::Text("Profiles: %d", (int)profiles.size());

        {
            bool canConfirm = std::abs(app.extrudeDistance) >= 0.01f;
            if (!canConfirm) ImGui::BeginDisabled();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
            if (ImGui::Button("Confirm Extrude", ImVec2(-1, 0))) {
                performExtrude();
            }
            ImGui::PopStyleColor();
            if (!canConfirm) ImGui::EndDisabled();
            if (ImGui::Button("Cancel", ImVec2(-1, 0))) {
                app.extrudePreviewing = false;
                app.extrudePreviewSolids.clear();
                app.extrudeInputActive = false;
                app.currentTool = Tool::Select;
            }
        }
    }

} else if (app.currentTool == Tool::Chamfer) {
    ImGui::Text("Chamfer");
    ImGui::Separator();
    ImGui::DragFloat("mm##chamDist", &app.chamferDistance, 0.1f, -1000.0f, 1000.0f, "%.2f");
    ImGui::Text("Selected edges: %d", (int)app.selectedEdgeSet.size());
    ImGui::TextWrapped("Click edges or corners (Shift adds), drag selected edge to adjust, Enter to apply.");
    if (ImGui::Button("Apply Chamfer [Enter]", ImVec2(-1, 0))) {
        applyActiveEdgeModifier();
    }
    if (ImGui::Button("Clear Selection", ImVec2(-1, 0))) {
        clearActiveEdgeModifierSelection();
        app.statusText = "Selection cleared";
    }

} else if (app.currentTool == Tool::Fillet) {
    ImGui::Text("Fillet");
    ImGui::Separator();
    ImGui::DragFloat("Radius##fil", &app.filletRadius, 0.1f, -1000.0f, 1000.0f, "%.2f mm");
    ImGui::Text("Selected edges: %d", (int)app.selectedEdgeSet.size());
    ImGui::TextWrapped("Click edges or corners (Shift adds), drag selected edge to adjust, Enter to apply.");
    if (ImGui::Button("Apply Fillet [Enter]", ImVec2(-1, 0))) {
        applyActiveEdgeModifier();
    }
    if (ImGui::Button("Clear Selection##fil", ImVec2(-1, 0))) {
        clearActiveEdgeModifierSelection();
        app.statusText = "Selection cleared";
    }

} else if (app.currentTool == Tool::Move) {
    ImGui::SeparatorText("MOVE");
    int count = (int)app.selectedSolidSet.size();
    if (count == 0 && app.selectedSolidIdx >= 0) count = 1;
    if (count == 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::textFaint);
        ImGui::TextWrapped("Click a body to select it, then drag an axis arrow.");
        ImGui::PopStyleColor();
    } else {
        theme::dimText("Selected"); ImGui::SameLine(96);
        ImGui::Text("%d %s", count, count == 1 ? "body" : "bodies");
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::textFaint);
        ImGui::TextWrapped("Drag an arrow, or nudge by an exact amount:");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        static const char* axisName[3] = { "X", "Y", "Z" };
        for (int a = 0; a < 3; a++) {
            ImGui::PushID(a);
            ImGui::SetNextItemWidth(96);
            ImGui::InputText(axisName[a], app.moveInputBuf[a], sizeof(app.moveInputBuf[a]),
                             ImGuiInputTextFlags_CharsDecimal);
            ImGui::SameLine();
            if (ImGui::Button("Apply", ImVec2(-1, 0))) {
                float v = (float)atof(app.moveInputBuf[a]);
                Vec3 d{0,0,0};
                if (a == 0) d.x = v; else if (a == 1) d.y = v; else d.z = v;
                moveSelectionBy(d, "Move");
                app.moveInputBuf[a][0] = '\0';
            }
            ImGui::PopID();
        }
    }

} else if (app.currentTool == Tool::Split) {
    ImGui::Text("Split");
    ImGui::Separator();
    if (app.selectedSolidIdx >= 0) {
        ImGui::Text("Body: %s",
            app.selectedSolidIdx < (int)app.solidNames.size()
                ? app.solidNames[app.selectedSolidIdx].c_str() : "?");
    } else {
        ImGui::TextColored(ImVec4(1,0.5f,0.3f,1), "Select a body first");
    }
    if (app.planePick == PlanePickMode::WaitingForPlane) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1), "Choose a plane");
    } else {
        ImGui::TextWrapped("Click a body, then click a construction plane, an "
                           "origin plane, or a face to cut with.");
    }

} else if (app.currentTool == Tool::ConstructionPlane) {
    ImGui::SeparatorText("PLANE");
    if (app.selectedConstructionPlaneIdx >= 0 &&
        app.selectedConstructionPlaneIdx < (int)app.constructionPlanes.size()) {
        auto& cp = app.constructionPlanes[app.selectedConstructionPlaneIdx];
        ImGui::Text("Name: %s", cp.name.c_str());
        Vec3 n = cp.plane.normal;
        ImGui::Text("Normal: (%.2f, %.2f, %.2f)", n.x, n.y, n.z);

        // Offset along the plane's own normal — the same value the drag
        // handle in the viewport edits.
        if (ImGui::DragFloat("Offset##cpoff", &cp.offset, 0.1f, -1000.0f, 1000.0f, "%.2f mm")) {
            cp.refresh();
        }
        if (ImGui::Button("Reset to face", ImVec2(-1, 0))) {
            cp.offset = 0.0f;
            cp.refresh();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, theme::textFaint);
        ImGui::TextWrapped("Drag the arrow in the viewport to slide the plane. "
                           "Click away from it to deselect.");
        ImGui::PopStyleColor();
        ImGui::DragFloat("Extent##cpext", &cp.extent, 0.5f, 1.0f, 500.0f, "%.1f mm");
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        if (ImGui::Button("Sketch on this Plane", ImVec2(-1, 0))) {
            enterSketchOnPlane(cp.plane, cp.origin, -1, -1, app.selectedConstructionPlaneIdx);
        }
        ImGui::PopStyleColor();
        if (ImGui::Button("Delete Plane", ImVec2(-1, 0))) {
            int idx = app.selectedConstructionPlaneIdx;
            // If the active sketch is on this plane, exit sketch first to
            // avoid leaving sketch mode pointing at a stale plane.
            if (app.inSketchMode && app.sketchSourcePlaneIdx == idx) {
                app.inSketchMode = false;
                app.sketchDrawing = false;
                app.dimInput.reset();
                app.sketchSourceSolidIdx = -1;
                app.sketchSourceFaceIdx = -1;
                app.sketchSourcePlaneIdx = -1;
                app.currentTool = Tool::Select;
            }
            undoStack.pushState("Delete plane");
            app.constructionPlanes.erase(app.constructionPlanes.begin() + idx);
            // Adjust any other index that referred to a later plane.
            if (app.sketchSourcePlaneIdx > idx) app.sketchSourcePlaneIdx--;
            app.selectedConstructionPlaneIdx = -1;
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::textFaint);
        ImGui::TextWrapped("Click a face to attach a plane to it, or click one of "
                           "the XY / XZ / YZ planes at the origin.");
        ImGui::PopStyleColor();
    }

} else if (app.currentTool == Tool::Join) {
    ImGui::SeparatorText("JOIN");
    int n = (int)app.selectedSolidSet.size();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::textFaint);
    ImGui::TextWrapped("Click bodies to add or remove them, then confirm.");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    for (int idx : app.selectedSolidSet) {
        if (idx < 0 || idx >= (int)app.solids.size()) continue;
        ImGui::BulletText("%s", idx < (int)app.solidNames.size()
            ? app.solidNames[idx].c_str() : "Body");
    }
    if (n == 0) ImGui::TextDisabled("Nothing selected");

    ImGui::Spacing();
    bool ready = n >= 2;
    if (!ready) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, theme::success);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::successHover);
    if (ImGui::Button(ready ? "Join selected  [Enter]" : "Join selected",
                      ImVec2(-1, theme::buttonHeight))) {
        performJoinSelection();
    }
    ImGui::PopStyleColor(2);
    if (!ready) ImGui::EndDisabled();

} else if (app.currentTool == Tool::SketchTrim) {
    ImGui::Text("Trim");
    ImGui::Separator();
    ImGui::TextWrapped("Click on a sketch segment to trim it at intersections.");

} else if (app.currentTool == Tool::SketchOffset) {
    ImGui::Text("Offset");
    ImGui::Separator();
    ImGui::DragFloat("Distance##ofs", &app.offsetDistance, 0.1f, 0.01f, 1000.0f, "%.2f mm");
    if (ImGui::Button("Apply Offset", ImVec2(-1, 0))) {
        undoStack.pushState("Offset profiles");
        app.sketch.offsetProfiles(app.offsetDistance);
        app.statusText = "Offset applied";
    }

} else if (app.inSketchMode) {
    ImGui::Text("Sketch Mode");
    ImGui::Separator();

    Vec3 n = app.sketch.sketchPlane.normal;
    const char* planeName = "Custom";
    if (std::abs(n.y) > 0.99f) planeName = "XY (Top)";
    else if (std::abs(n.z) > 0.99f) planeName = "XZ (Front)";
    else if (std::abs(n.x) > 0.99f) planeName = "YZ (Side)";
    ImGui::Text("Plane: %s", planeName);
    ImGui::Text("Units: mm");
    ImGui::Separator();

    ImGui::Text("Entities: %d", (int)app.sketch.entities.size());
    ImGui::DragFloat("Grid mm", &app.sketch.gridSize, 0.5f, 0.5f, 100.0f, "%.1f");
    ImGui::Checkbox("Snap to Grid", &app.sketch.snapToGrid);
    ImGui::DragFloat("Snap mm", &app.sketch.snapDistance, 0.1f, 0.1f, 10.0f, "%.1f");
    ImGui::Separator();

    if (app.sketchDrawing) {
        Vec2 delta = app.sketchDrawCurrent - app.sketchDrawStart;
        float len = delta.length();
        float ang = std::atan2(delta.y, delta.x) * RAD2DEG;
        ImGui::Text("Length: %.2f mm", len);
        ImGui::Text("Angle:  %.1f deg", ang);
        ImGui::Text("dX: %.2f  dY: %.2f mm", delta.x, delta.y);
    }

    ImGui::Separator();
    if (ImGui::Button("Exit Sketch [Esc]", ImVec2(-1, 0))) {
        app.inSketchMode = false;
        app.sketchDrawing = false;
        app.dimInput.reset();
        app.sketchSourceSolidIdx = -1;
        app.sketchSourceFaceIdx = -1;
        app.sketchSourceFaceLoop.clear();
        app.currentTool = Tool::Select;
    }
    if (ImGui::Button("Clear Sketch", ImVec2(-1, 0))) {
        undoStack.pushState("Clear sketch");
        app.sketch.clear();
    }
} else {
    bool hasBody = app.selectedSolidIdx >= 0 && app.selectedSolidIdx < (int)app.solids.size();

    if (!hasBody) {
        // Summarise the whole model rather than leaving the panel blank —
        // overall size and triangle count are what you check before exporting.
        AABB all;
        size_t faceCount = 0, edgeCount = 0;
        for (const auto& s : app.solids) {
            AABB b = s.bounds();
            all.expand(b.min);
            all.expand(b.max);
            faceCount += s.faces.size();
            edgeCount += s.edges.size();
        }

        ImGui::SeparatorText("MODEL");
        theme::dimText("Units");  ImGui::SameLine(100); ImGui::Text("mm");
        theme::dimText("Bodies"); ImGui::SameLine(100); ImGui::Text("%d", (int)app.solids.size());
        if (!app.solids.empty()) {
            Vec3 sz = all.size();
            theme::dimText("Extent"); ImGui::SameLine(100);
            ImGui::Text("%.1f x %.1f x %.1f", sz.x, sz.y, sz.z);
            theme::dimText("Faces");  ImGui::SameLine(100); ImGui::Text("%d", (int)faceCount);
            theme::dimText("Edges");  ImGui::SameLine(100); ImGui::Text("%d", (int)edgeCount);
        }

        ImGui::SeparatorText("VIEW");
        ImGui::Checkbox("Show edges", &app.showSolidEdges);
        if (ImGui::Button("Fit to model", ImVec2(-1, theme::buttonHeight))) {
            fitViewToModel();
            app.statusText = "View fit to model";
        }

        if (!app.solids.empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, theme::textFaint);
            ImGui::TextWrapped("Click a body, face or edge to see its dimensions.");
            ImGui::PopStyleColor();
        }
    } else {
        const auto& s = app.solids[app.selectedSolidIdx];
        std::string name = (app.selectedSolidIdx < (int)app.solidNames.size())
            ? app.solidNames[app.selectedSolidIdx] : ("Body " + std::to_string(app.selectedSolidIdx));

        ImGui::SeparatorText("SELECTION");
        ImGui::TextUnformatted(name.c_str());
        ImGui::Spacing();

        AABB bb = s.bounds();
        Vec3 sz = bb.size();
        theme::dimText("Size");   ImGui::SameLine(96); ImGui::Text("%.1f x %.1f x %.1f", sz.x, sz.y, sz.z);
        theme::dimText("Faces");  ImGui::SameLine(96); ImGui::Text("%d", (int)s.faces.size());
        theme::dimText("Edges");  ImGui::SameLine(96); ImGui::Text("%d", (int)s.edges.size());

        // Sub-selection detail — this is the number the user is usually after.
        if (app.selectedFaceIdx >= 0 && app.selectedFaceIdx < (int)s.faces.size()) {
            const auto& f = s.faces[app.selectedFaceIdx];
            ImGui::SeparatorText("FACE");
            theme::dimText("Area");   ImGui::SameLine(96); ImGui::Text("%.2f mm2", polygonArea(f.outerLoop));
            theme::dimText("Sides");  ImGui::SameLine(96); ImGui::Text("%d", (int)f.outerLoop.size());
            theme::dimText("Normal"); ImGui::SameLine(96);
            ImGui::Text("%.2f, %.2f, %.2f", f.normal.x, f.normal.y, f.normal.z);
        }
        if (app.selectedEdgeIdx >= 0 && app.selectedEdgeIdx < (int)s.edges.size()) {
            const auto& e = s.edges[app.selectedEdgeIdx];
            ImGui::SeparatorText("EDGE");
            theme::dimText("Length"); ImGui::SameLine(96); ImGui::Text("%.2f mm", e.length());
            Vec3 m = e.midpoint();
            theme::dimText("Middle"); ImGui::SameLine(96);
            ImGui::Text("%.1f, %.1f, %.1f", m.x, m.y, m.z);
        }

        ImGui::SeparatorText("APPEARANCE");
        if (app.selectedSolidIdx < (int)app.solidColors.size()) {
            Color& col = app.solidColors[app.selectedSolidIdx];
            float c[3] = {col.r, col.g, col.b};
            if (ImGui::ColorEdit3("##color", c, ImGuiColorEditFlags_NoInputs)) {
                if (ImGui::IsItemDeactivatedAfterEdit() || !ImGui::IsItemActive()) {
                    undoStack.pushState("Change color");
                }
                col.r = c[0]; col.g = c[1]; col.b = c[2];
                app.solids[app.selectedSolidIdx].color = col;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted("Colour");
        }
        ImGui::Checkbox("Show edges", &app.showSolidEdges);

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(theme::danger.x, theme::danger.y, theme::danger.z, 0.16f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::danger);
        if (ImGui::Button("Delete Body", ImVec2(-1, theme::buttonHeight))) {
            undoStack.pushState("Delete body");
            removeSolid(app.selectedSolidIdx);
        }
        ImGui::PopStyleColor(2);
    }
}

    ImGui::End();
}

static void drawShortcutOverlay();
static void drawContextMenu();
static void drawStatusBar();

// Horizontal centre of the area the user can actually see, which is the window
// minus whichever panels are showing.  Centring on the raw window would push
// content behind a panel.
static float viewportCentreX() {
    float left  = app.panelsVisible ? theme::panelWidth : 0.0f;
    float right = app.panelsVisible ? theme::propsWidth : 0.0f;
    return left + ((float)app.windowWidth - left - right) * 0.5f;
}

// Shown when the document is empty.  Without this the app opens to a bare grid
// with no indication of where to begin.
static void drawEmptyState() {
    if (!app.solids.empty() || app.inSketchMode) return;
    if (app.planePick == PlanePickMode::WaitingForPlane) return;

    ImGui::SetNextWindowPos(ImVec2(viewportCentreX(), app.windowHeight * 0.52f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28, 24));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    // Darker than the buttons it contains, or the buttons vanish into it.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.071f, 0.078f, 0.094f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_Button, theme::bgActive);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.271f, 0.294f, 0.341f, 1.0f));
    ImGui::Begin("##EmptyState", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);

    ImGui::PushStyleColor(ImGuiCol_Text, theme::text);
    ImGui::TextUnformatted("Nothing here yet");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::textDim);
    ImGui::TextUnformatted("Draw a profile on a plane, then extrude it.");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::Spacing();

    if (ImGui::Button("Draw a rectangle", ImVec2(168, theme::buttonHeight)))
        startSketchTool(Tool::SketchRect);
    ImGui::SameLine();
    if (ImGui::Button("Draw a circle", ImVec2(150, theme::buttonHeight)))
        startSketchTool(Tool::SketchCircle);
    ImGui::SameLine();
    if (ImGui::Button("Add a box", ImVec2(120, theme::buttonHeight))) {
        undoStack.pushState("Add box");
        addSolid(makeBox({0, 5.0f, 0}, {10, 10, 10}));
        app.statusText = "Added 10 mm box";
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::textFaint);
    ImGui::TextUnformatted("or press  L  C  R  to start sketching  ·  Cmd+O to open a file");
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleColor(3);  // WindowBg, Button, ButtonHovered
    ImGui::PopStyleVar(2);    // WindowPadding, WindowRounding
}

// A thin vertical rule separating toolbar groups.  Groups are what make a
// twelve-button bar scannable — without them it reads as one long run.
static void toolbarDivider() {
    // One SameLine opens the whole gap; a second call would not accumulate, so
    // the rule is drawn back into the middle of the gap that this one creates.
    ImGui::SameLine(0, 23);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = theme::buttonHeight;
    ImU32 col = ImGui::GetColorU32(ImVec4(0.32f, 0.35f, 0.42f, 1.0f));
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(p.x - 11.5f, p.y + 4), ImVec2(p.x - 11.5f, p.y + h - 4), col, 1.0f);
}

void drawUI() {
    // ---- Toolbar ----
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)app.windowWidth, theme::toolbarHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 11));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::bgPanel);
    ImGui::Begin("##Toolbar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoCollapse);

    auto toolBtn = [](const char* label, const char* shortcut, const char* help,
                      Tool tool, bool isSketch = false) {
        bool active = (app.currentTool == tool);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, theme::accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::accentHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::accent);
        }
        float btnW = ImGui::CalcTextSize(label).x + 22.0f;
        if (ImGui::Button(label, ImVec2(btnW, theme::buttonHeight))) {
            // Clicking the already-active tool exits it back to Select so a
            // user who accidentally entered (e.g.) Chamfer can leave with one
            // click instead of needing Esc.
            if (active && tool != Tool::Select) {
                app.currentTool = Tool::Select;
                app.statusText = "Ready";
            } else if (isSketch) {
                startSketchTool(tool);
            } else if (tool == Tool::Extrude) {
                enterExtrudeMode();
            } else if (tool == Tool::ConstructionPlane) {
                app.currentTool = tool;
                app.selectedConstructionPlaneIdx = -1;
                app.statusText = "Plane — click a face to attach a plane to it";
            } else if (tool == Tool::Join) {
                // Start from a clean slate so a stray earlier selection cannot
                // silently become part of the merge.
                app.selectedSolidSet.clear();
                app.selectedSolidIdx = -1;
                app.currentTool = tool;
                app.statusText = "Join — click two or more bodies, then confirm";
            } else {
                app.currentTool = tool;
            }
        }
        if (active) ImGui::PopStyleColor(3);

        // Every tool explains itself, with its key — the shortcuts are useless
        // if the only place they appear is a hidden overlay.
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(help);
            if (shortcut && shortcut[0]) {
                ImGui::SameLine(0, 10);
                ImGui::TextColored(theme::textFaint, "%s", shortcut);
            }
            ImGui::EndTooltip();
        }
        ImGui::SameLine();
    };

    toolBtn("Select", "Esc", "Select bodies, faces and edges", Tool::Select);
    toolbarDivider();

    toolBtn("Line",   "L", "Draw a line on a plane",      Tool::SketchLine,   true);
    toolBtn("Circle", "C", "Draw a circle on a plane",    Tool::SketchCircle, true);
    toolBtn("Rect",   "R", "Draw a rectangle on a plane", Tool::SketchRect,   true);
    toolbarDivider();

    toolBtn("Extrude", "E",       "Pull a profile or face into a solid", Tool::Extrude);
    toolBtn("Move",    "G",       "Drag a body along X, Y or Z",         Tool::Move);
    toolBtn("Chamfer", "Shift+C", "Cut a flat bevel on an edge",         Tool::Chamfer);
    toolBtn("Fillet",  "Shift+F", "Round an edge",                       Tool::Fillet);
    toolBtn("Split",   "Shift+S", "Cut a body with a plane",             Tool::Split);
    toolBtn("Join",    "",        "Merge two bodies into one",           Tool::Join);
    toolBtn("Plane",   "",        "Create a reference plane",            Tool::ConstructionPlane);
    toolBtn("Measure", "M",       "Measure distance between two points", Tool::Measure);

    // Finish sketch — the one primary action, so it gets the only green.
    if (app.inSketchMode) {
        toolbarDivider();
        ImGui::PushStyleColor(ImGuiCol_Button, theme::success);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::successHover);
        if (ImGui::Button("Finish Sketch", ImVec2(112, theme::buttonHeight))) {
            app.inSketchMode = false;
            app.sketchDrawing = false;
            app.dimInput.reset();
            app.sketchSourceSolidIdx = -1;
            app.sketchSourceFaceIdx = -1;
            app.sketchSourceFaceLoop.clear();
            app.currentTool = Tool::Select;
            app.statusText = "Sketch finished";
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
    }

    // ---- Right-aligned: history, then file actions ----
    const float rightBlockW = 60 + 60 + 21 + 62 + 62 + 96 + 3 * 4;
    float rightX = ImGui::GetWindowWidth() - rightBlockW - 10.0f;
    if (rightX > ImGui::GetCursorPosX()) ImGui::SameLine(rightX);
    else ImGui::SameLine(0, 16);

    bool canUndo = undoStack.canUndo();
    if (!canUndo) ImGui::BeginDisabled();
    if (ImGui::Button("Undo", ImVec2(60, theme::buttonHeight))) undoStack.undo();
    if (!canUndo) ImGui::EndDisabled();
    if (canUndo && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Undo %s   Cmd+Z", undoStack.undoDescription().c_str());
    ImGui::SameLine();

    bool canRedo = undoStack.canRedo();
    if (!canRedo) ImGui::BeginDisabled();
    if (ImGui::Button("Redo", ImVec2(60, theme::buttonHeight))) undoStack.redo();
    if (!canRedo) ImGui::EndDisabled();
    if (canRedo && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Redo %s   Cmd+Shift+Z", undoStack.redoDescription().c_str());

    toolbarDivider();

    if (ImGui::Button("Save", ImVec2(62, theme::buttonHeight))) saveProjectInteractive();
    ImGui::SameLine();
    if (ImGui::Button("Open", ImVec2(62, theme::buttonHeight))) loadProjectInteractive();
    ImGui::SameLine();
    if (ImGui::Button("Export", ImVec2(96, theme::buttonHeight))) openExportStlDialog();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Export STL   Cmd+Shift+E");

    // Hairline under the toolbar, separating it from the viewport.
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(0, theme::toolbarHeight - 1),
        ImVec2((float)app.windowWidth, theme::toolbarHeight - 1),
        ImGui::GetColorU32(theme::border), 1.0f);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    drawExportStlDialog();
    drawEmptyState();

    if (!app.panelsVisible) {
        drawShortcutOverlay();
        drawContextMenu();
        drawStatusBar();
        return;
    }

    // ---- Model tree (left side) ----
    ImGui::SetNextWindowPos(ImVec2(0, theme::toolbarHeight));
    ImGui::SetNextWindowSize(ImVec2(theme::panelWidth,
        (float)app.windowHeight - theme::toolbarHeight - theme::statusHeight));
    ImGui::Begin("##ModelTree", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse);

    ImGui::SeparatorText("BODIES");
    {
        if (app.solids.empty()) {
            ImGui::TextDisabled("Nothing yet");
        }
        for (int i = 0; i < (int)app.solids.size(); i++) {
            ImGui::PushID(i);
            // Visibility toggle
            bool vis = (i < (int)app.solidVisible.size()) ? app.solidVisible[i] : true;
            if (ImGui::Checkbox("##vis", &vis)) {
                if (i < (int)app.solidVisible.size()) app.solidVisible[i] = vis;
            }
            ImGui::SameLine();

            // Selectable name — double-click to rename in place.
            bool selected = (app.selectedSolidIdx == i) ||
                            (app.selectedSolidSet.count(i) > 0);
            std::string name = (i < (int)app.solidNames.size()) ? app.solidNames[i] : ("Body " + std::to_string(i));

            if (app.renamingSolidIdx == i) {
                ImGui::SetNextItemWidth(-1);
                if (app.renameFocusPending) {
                    ImGui::SetKeyboardFocusHere();
                    app.renameFocusPending = false;
                }
                if (ImGui::InputText("##rename", app.renameBuf, sizeof(app.renameBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue |
                                     ImGuiInputTextFlags_AutoSelectAll)) {
                    if (i < (int)app.solidNames.size() && app.renameBuf[0])
                        app.solidNames[i] = app.renameBuf;
                    app.renamingSolidIdx = -1;
                }
                if (ImGui::IsItemDeactivated()) app.renamingSolidIdx = -1;
                ImGui::PopID();
                continue;
            }

            // Colour chip, so bodies are identifiable at a glance rather than
            // by reading "Body 1" / "Body 2".
            {
                Color bc = (i < (int)app.solidColors.size()) ? app.solidColors[i]
                                                             : app.solids[i].color;
                ImVec2 cp = ImGui::GetCursorScreenPos();
                float sz = ImGui::GetTextLineHeight() * 0.62f;
                float oy = (ImGui::GetTextLineHeight() - sz) * 0.5f;
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(cp.x, cp.y + oy), ImVec2(cp.x + sz, cp.y + oy + sz),
                    ImGui::GetColorU32(ImVec4(bc.r, bc.g, bc.b, 1.0f)), 2.0f);
                ImGui::Dummy(ImVec2(sz + 7.0f, ImGui::GetTextLineHeight()));
                ImGui::SameLine(0, 0);
            }

            if (ImGui::Selectable(name.c_str(), selected)) {
                ImGuiIO& io = ImGui::GetIO();
                bool extend = io.KeyShift || io.KeySuper || io.KeyCtrl;
                if (!extend) {
                    app.selectedSolidSet.clear();
                    app.selectedFaceSet.clear();
                    app.selectedEdgeSet.clear();
                    app.selectedFaceIdx = -1;
                    app.selectedEdgeIdx = -1;
                }
                if (extend && app.selectedSolidSet.count(i)) {
                    app.selectedSolidSet.erase(i);
                    if (app.selectedSolidIdx == i) {
                        app.selectedSolidIdx = app.selectedSolidSet.empty()
                            ? -1 : *app.selectedSolidSet.begin();
                    }
                } else {
                    app.selectedSolidIdx = i;
                    app.selectedSolidSet.insert(i);
                }
            }

            // Right-click context menu on body
            if (ImGui::BeginPopupContextItem("body_ctx")) {
                if (ImGui::MenuItem("Delete")) {
                    undoStack.pushState("Delete body");
                    removeSolid(i);
                    app.selectedSolidIdx = -1;
                    ImGui::EndPopup();
                    ImGui::PopID();
                    break;
                }
                if (ImGui::MenuItem("Rename")) {
                    app.renamingSolidIdx = i;
                    app.renameFocusPending = true;
                    std::snprintf(app.renameBuf, sizeof(app.renameBuf), "%s", name.c_str());
                }
                ImGui::EndPopup();
            }

            // Double-click the name to rename, matching every file browser.
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                app.renamingSolidIdx = i;
                app.renameFocusPending = true;
                std::snprintf(app.renameBuf, sizeof(app.renameBuf), "%s", name.c_str());
            }
            ImGui::PopID();
        }
    }

    // Empty sections are hidden rather than shown with a placeholder — a panel
    // that says "No planes / No sketch" is spending space to say nothing.
    if (!app.constructionPlanes.empty()) {
        ImGui::SeparatorText("PLANES");
        {
            for (int i = 0; i < (int)app.constructionPlanes.size(); i++) {
                ImGui::PushID(1000 + i);
                auto& cp = app.constructionPlanes[i];
                if (ImGui::Checkbox("##cpvis", &cp.visible)) {}
                ImGui::SameLine();
                bool sel = (app.selectedConstructionPlaneIdx == i);
                if (ImGui::Selectable(cp.name.c_str(), sel)) {
                    // Route into Split tool's "awaiting plane" step so the user
                    // can split a body by a construction plane simply by
                    // clicking the plane in the tree.
                    if (app.currentTool == Tool::Split &&
                        app.planePick == PlanePickMode::WaitingForPlane &&
                        app.selectedSolidIdx >= 0 &&
                        app.selectedSolidIdx < (int)app.solids.size()) {
                        auto [above, below] = split(app.solids[app.selectedSolidIdx], cp.plane);
                        undoStack.pushState("Split");
                        app.solids[app.selectedSolidIdx] = above;
                        std::string srcName = (app.selectedSolidIdx < (int)app.solidNames.size())
                            ? app.solidNames[app.selectedSolidIdx] : std::string("Body");
                        addSolid(below, srcName + " (split)");
                        app.planePick = PlanePickMode::None;
                        app.currentTool = Tool::Select;
                        app.statusText = "Split by " + cp.name;
                    } else {
                        app.selectedConstructionPlaneIdx = i;
                        app.currentTool = Tool::ConstructionPlane;
                    }
                }
                if (ImGui::BeginPopupContextItem("cp_ctx")) {
                    if (ImGui::MenuItem("Sketch on plane")) {
                    enterSketchOnPlane(cp.plane, cp.origin, -1, -1, i);
                }
                if (ImGui::MenuItem("Delete")) {
                    if (app.inSketchMode && app.sketchSourcePlaneIdx == i) {
                        app.inSketchMode = false;
                        app.sketchDrawing = false;
                        app.dimInput.reset();
                        app.sketchSourceSolidIdx = -1;
                        app.sketchSourceFaceIdx = -1;
                        app.sketchSourcePlaneIdx = -1;
                        app.currentTool = Tool::Select;
                    }
                    undoStack.pushState("Delete plane");
                    app.constructionPlanes.erase(app.constructionPlanes.begin() + i);
                    if (app.sketchSourcePlaneIdx > i) app.sketchSourcePlaneIdx--;
                    if (app.selectedConstructionPlaneIdx >= (int)app.constructionPlanes.size())
                        app.selectedConstructionPlaneIdx = -1;
                    ImGui::EndPopup();
                    ImGui::PopID();
                    break;
                }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
        }
    }

    if (!app.sketch.entities.empty()) {
        ImGui::SeparatorText("SKETCH");
        ImGui::PushID("sketch0");
        if (ImGui::Checkbox("##skvis", &app.sketchVisible)) {}
        ImGui::SameLine();
        const char* skLabel = app.inSketchMode ? "Sketch (active)" : "Sketch";
        ImGui::Selectable(skLabel, app.inSketchMode);
        theme::dimText("%d entities", (int)app.sketch.entities.size());
        if (!app.inSketchMode) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Edit")) {
                app.inSketchMode = true;
                app.currentTool = Tool::SketchLine;
                app.statusText = "Re-entered sketch";
            }
        }
        ImGui::PopID();
    }

    // Pin "Add box" to the bottom of the panel.  It is a creation action, not
    // part of the tree above it, and anchoring it stops it from drifting up
    // and down as bodies come and go.
    {
        float btnH = theme::buttonHeight;
        float y = ImGui::GetWindowHeight() - btnH - ImGui::GetStyle().WindowPadding.y;
        if (y > ImGui::GetCursorPosY()) ImGui::SetCursorPosY(y);
        if (ImGui::Button("Add box", ImVec2(-1, btnH))) {
            undoStack.pushState("Add box");
            addSolid(makeBox({0, 5.0f, 0}, {10, 10, 10}));
            app.statusText = "Added 10 mm box";
        }
    }

    ImGui::End();

    drawPropertiesPanel();

    drawPlanePickerUI();
    drawDimensionInput();

    // ---- Extrude Distance Input Bar ----
    if (app.extrudeInputActive && app.currentTool == Tool::Extrude && app.extrudePreviewing) {
        float barW = 240;
        float barH = 50;
        float barX = ((float)app.windowWidth - barW) * 0.5f;
        float barY = (float)app.windowHeight - 90;

        ImGui::SetNextWindowPos(ImVec2(barX, barY));
        ImGui::SetNextWindowSize(ImVec2(barW, barH));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.15f, 0.95f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::Begin("##ExtrudeInput", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoCollapse);

        ImGui::Text("Distance:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        if (app.extrudeInputFocus) {
            ImGui::SetKeyboardFocusHere();
            app.extrudeInputFocus = false;
        }
        char hint[32] = "";
        if (std::abs(app.extrudeDistance) > 0.01f)
            std::snprintf(hint, sizeof(hint), "%.1f", app.extrudeDistance);
        if (ImGui::InputTextWithHint("mm##extDist", hint, app.extrudeInputBuf, sizeof(app.extrudeInputBuf),
                ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
            float val = (float)std::atof(app.extrudeInputBuf);
            if (std::abs(val) > 0.0f) {
                app.extrudeDistance = val;
                updateExtrudePreview();
                performExtrude();
            }
        }
        // Live update preview as user types
        if (std::strlen(app.extrudeInputBuf) > 0) {
            float val = (float)std::atof(app.extrudeInputBuf);
            if (std::abs(val) > 0.0f && std::abs(val - app.extrudeDistance) > 0.01f) {
                app.extrudeDistance = val;
                updateExtrudePreview();
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    drawShortcutOverlay();
    drawContextMenu();
    drawStatusBar();
}

// ---- Shortcut Overlay ----
static void drawShortcutOverlay() {
    if (app.showShortcutOverlay) {
        ImGui::SetNextWindowPos(ImVec2(app.windowWidth * 0.5f, app.windowHeight * 0.5f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(420, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 18));
        ImGui::Begin("Shortcuts", &app.showShortcutOverlay,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_AlwaysAutoResize);

        // Navigation comes first: it is what a new user needs before any tool
        // is useful, and it is the part that differs on a trackpad.
        struct Row { const char* key; const char* what; };
        auto table = [](const char* title, const Row* rows, int n) {
            ImGui::SeparatorText(title);
            if (ImGui::BeginTable(title, 2, ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 168.0f);
                ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
                for (int i = 0; i < n; i++) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(theme::accent, "%s", rows[i].key);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(rows[i].what);
                }
                ImGui::EndTable();
            }
            ImGui::Spacing();
        };

        static const Row nav[] = {
            {"Option + drag",         "Orbit"},
            {"Shift + Option + drag", "Pan"},
            {"Scroll / pinch",        "Zoom to cursor"},
            {"Shift + scroll",        "Pan"},
            {"Right / middle drag",   "Orbit (with a mouse)"},
            {"F  or  Home",           "Fit view to model"},
        };
        static const Row tools[] = {
            {"L / C / R",   "Line / Circle / Rectangle"},
            {"E",           "Extrude"},
            {"Shift+C",     "Chamfer"},
            {"Shift+F",     "Fillet"},
            {"Shift+S",     "Split"},
            {"M",           "Measure"},
            {"Esc",         "Back to Select / cancel"},
            {"Enter",       "Confirm / finish sketch"},
        };
        static const Row file[] = {
            {"Cmd+Z",       "Undo"},
            {"Cmd+Shift+Z", "Redo"},
            {"Cmd+S",       "Save"},
            {"Cmd+O",       "Open"},
            {"Cmd+Shift+E", "Export STL"},
            {"?",           "Show or hide this panel"},
        };

        table("NAVIGATION", nav, IM_ARRAYSIZE(nav));
        table("TOOLS", tools, IM_ARRAYSIZE(tools));
        table("FILE", file, IM_ARRAYSIZE(file));

        ImGui::End();
        ImGui::PopStyleVar();
    }

}

// ---- Right-click context menu ----
static void drawContextMenu() {
    if (ImGui::BeginPopup("ContextMenu")) {
        if (app.selectedSolidIdx >= 0) {
            if (ImGui::MenuItem("Delete Body")) {
                undoStack.pushState("Delete body");
                removeSolid(app.selectedSolidIdx);
                app.selectedSolidIdx = -1;
            }
            if (ImGui::MenuItem("Edit Sketch on Face")) {
                if (app.selectedFaceIdx >= 0 && app.selectedSolidIdx >= 0) {
                    const auto& face = app.solids[app.selectedSolidIdx].faces[app.selectedFaceIdx];
                    enterSketchOnPlane(Plane(face.normal, face.outerLoop[0]), face.centroid());
                }
            }
        }
        if (ImGui::MenuItem("Add Box")) {
            undoStack.pushState("Add box");
            addSolid(makeBox({0, 5.0f, 0}, {10, 10, 10}));
        }
        ImGui::EndPopup();
    }

    // Open the context menu on *release*, and only when the right button
    // didn't travel — otherwise every right-drag orbit would pop the menu.
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        !ImGui::GetIO().WantCaptureMouse && !app.rightDragMoved) {
        ImGui::OpenPopup("ContextMenu");
    }

}

// ---- Status bar ----
static void drawStatusBar() {
    ImGui::SetNextWindowPos(ImVec2(0, (float)app.windowHeight - theme::statusHeight));
    ImGui::SetNextWindowSize(ImVec2((float)app.windowWidth, theme::statusHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 6));
    ImGui::Begin("##Status", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(0, (float)app.windowHeight - theme::statusHeight),
        ImVec2((float)app.windowWidth, (float)app.windowHeight - theme::statusHeight),
        ImGui::GetColorU32(theme::border), 1.0f);

    ImGui::TextUnformatted(app.statusText.c_str());

    if (app.inSketchMode) {
        ImGui::SameLine(0, 16);
        ImGui::TextColored(theme::accent, "Sketch");

        Ray ray = app.camera.screenToRay(app.mouseX, app.mouseY);
        float t = app.sketch.sketchPlane.intersectRay(ray);
        if (t > 0) {
            Vec2 sp = app.sketch.worldToSketch(ray.at(t));
            ImGui::SameLine(0, 12);
            theme::dimText("%.1f, %.1f mm", sp.x, sp.y);
        }
    }

    // Navigation hint, right-aligned.  These three gestures are the whole
    // difference between the app feeling usable on a laptop and not, so they
    // live permanently on screen rather than inside a help overlay.
    const char* hint = "Option+drag orbit   ·   Shift+Option+drag pan   ·   Scroll zoom   ·   ? shortcuts";
    float hintW = ImGui::CalcTextSize(hint).x;
    float hintX = ImGui::GetWindowWidth() - hintW - 14.0f;
    if (hintX > ImGui::GetCursorPosX() + 20.0f) {
        ImGui::SameLine(hintX);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::textFaint);
        ImGui::TextUnformatted(hint);
        ImGui::PopStyleColor();
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

// ---- Draw XYZ Plane Indicators at Origin ----
void drawPlaneIndicators() {
    if (app.planePick != PlanePickMode::WaitingForPlane && !app.inSketchMode) return;

    float size = 8.0f;
    float thick = 2.5f;

    // XY plane (Top) — green, filled outline + diagonals
    Color xyC = {0.1f, 0.85f, 0.1f, 0.6f};
    app.renderer.drawLine3D({-size, 0, -size}, {size, 0, -size}, app.camera, xyC, thick);
    app.renderer.drawLine3D({size, 0, -size}, {size, 0, size}, app.camera, xyC, thick);
    app.renderer.drawLine3D({size, 0, size}, {-size, 0, size}, app.camera, xyC, thick);
    app.renderer.drawLine3D({-size, 0, size}, {-size, 0, -size}, app.camera, xyC, thick);
    app.renderer.drawLine3D({-size, 0, -size}, {size, 0, size}, app.camera, {0.1f, 0.85f, 0.1f, 0.25f}, 1.0f);
    app.renderer.drawLine3D({size, 0, -size}, {-size, 0, size}, app.camera, {0.1f, 0.85f, 0.1f, 0.25f}, 1.0f);

    // XZ plane (Front) — blue
    Color xzC = {0.1f, 0.1f, 0.95f, 0.6f};
    app.renderer.drawLine3D({-size, -size, 0}, {size, -size, 0}, app.camera, xzC, thick);
    app.renderer.drawLine3D({size, -size, 0}, {size, size, 0}, app.camera, xzC, thick);
    app.renderer.drawLine3D({size, size, 0}, {-size, size, 0}, app.camera, xzC, thick);
    app.renderer.drawLine3D({-size, size, 0}, {-size, -size, 0}, app.camera, xzC, thick);
    app.renderer.drawLine3D({-size, -size, 0}, {size, size, 0}, app.camera, {0.1f, 0.1f, 0.95f, 0.25f}, 1.0f);
    app.renderer.drawLine3D({size, -size, 0}, {-size, size, 0}, app.camera, {0.1f, 0.1f, 0.95f, 0.25f}, 1.0f);

    // YZ plane (Side) — red
    Color yzC = {0.95f, 0.1f, 0.1f, 0.6f};
    app.renderer.drawLine3D({0, -size, -size}, {0, size, -size}, app.camera, yzC, thick);
    app.renderer.drawLine3D({0, size, -size}, {0, size, size}, app.camera, yzC, thick);
    app.renderer.drawLine3D({0, size, size}, {0, -size, size}, app.camera, yzC, thick);
    app.renderer.drawLine3D({0, -size, size}, {0, -size, -size}, app.camera, yzC, thick);
    app.renderer.drawLine3D({0, -size, -size}, {0, size, size}, app.camera, {0.95f, 0.1f, 0.1f, 0.25f}, 1.0f);
    app.renderer.drawLine3D({0, size, -size}, {0, -size, size}, app.camera, {0.95f, 0.1f, 0.1f, 0.25f}, 1.0f);
}
