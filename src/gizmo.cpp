#include "gizmo.h"
#include "input.h"
#include "camera.h"
#include <imgui.h>
#include <cmath>
#include <cstring>

using namespace cad;

// Axis endpoint info
struct AxisEnd {
    Vec3 dir;
    const char* viewName;  // "Right", "Front", etc.
    const char* axisChar;  // "X", "Y", "Z" (only for positive)
    float yaw, pitch;
    ImU32 color;
    ImU32 colorHover;
    bool positive;
    int axisIdx; // 0=X, 1=Y, 2=Z
};

static const AxisEnd axisEnds[] = {
    // +X = Right
    {{1,0,0}, "Right", "X",  0.0f,   0.0f, IM_COL32(220, 60, 60, 255), IM_COL32(255,120,120,255), true, 0},
    // -X = Left
    {{-1,0,0}, "Left", nullptr, 180.0f, 0.0f, IM_COL32(160, 50, 50, 180), IM_COL32(220, 90, 90,255), false, 0},
    // +Y = Top
    {{0,1,0}, "Top", "Y",   45.0f,  89.0f, IM_COL32(60,190, 60,255), IM_COL32(100,230,100,255), true, 1},
    // -Y = Bottom
    {{0,-1,0}, "Bottom", nullptr, 45.0f, -89.0f, IM_COL32(40,130,40,180), IM_COL32(80,170,80,255), false, 1},
    // +Z = Front
    {{0,0,1}, "Front", "Z",  90.0f,  0.0f, IM_COL32(60, 60,220,255), IM_COL32(100,100,255,255), true, 2},
    // -Z = Back
    {{0,0,-1}, "Back", nullptr, 270.0f, 0.0f, IM_COL32(40,40,160,180), IM_COL32(80,80,210,255), false, 2},
};

static ImU32 applyAlpha(ImU32 col, float alpha) {
    int r = (col >> 0) & 0xFF;
    int g = (col >> 8) & 0xFF;
    int b = (col >> 16) & 0xFF;
    int a = (int)(((col >> 24) & 0xFF) * alpha);
    return IM_COL32(r, g, b, a);
}

void drawViewGizmo() {
    const float gizmoSize = 130.0f;
    const float margin = 12.0f;
    const float axisLen = gizmoSize * 0.34f;
    const float cx = (float)app.windowWidth - gizmoSize * 0.5f - margin - 220.0f;
    const float cy = 50.0f + gizmoSize * 0.5f + margin;

    float yr = app.camera.yaw * DEG2RAD;
    float pr = app.camera.pitch * DEG2RAD;

    Vec3 camFwd = {
        -std::cos(pr) * std::cos(yr),
        -std::sin(pr),
        -std::cos(pr) * std::sin(yr)
    };
    Vec3 camRight = {std::sin(yr), 0.0f, -std::cos(yr)};
    Vec3 camUp = camFwd.cross(camRight).normalized();

    auto project = [&](Vec3 dir) -> ImVec2 {
        float x = dir.dot(camRight);
        float y = dir.dot(camUp);
        return ImVec2(cx + x * axisLen, cy - y * axisLen);
    };
    auto depthOf = [&](Vec3 dir) -> float {
        return dir.dot(camFwd);
    };

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // Background
    dl->AddCircleFilled(ImVec2(cx, cy), gizmoSize * 0.46f, IM_COL32(25, 25, 30, 200), 48);
    dl->AddCircle(ImVec2(cx, cy), gizmoSize * 0.46f, IM_COL32(70, 70, 80, 180), 48, 1.5f);

    // Sort all 6 endpoints by depth (back-to-front)
    int order[6] = {0,1,2,3,4,5};
    float depths[6];
    for (int i = 0; i < 6; i++) depths[i] = depthOf(axisEnds[i].dir);
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 5-i; j++)
            if (depths[order[j]] > depths[order[j+1]])
                std::swap(order[j], order[j+1]);

    ImU32 lineColors[3] = {
        IM_COL32(220, 60, 60, 255),
        IM_COL32(60, 190, 60, 255),
        IM_COL32(60, 60, 220, 255),
    };

    ImVec2 mousePos = ImGui::GetMousePos();

    for (int oi = 0; oi < 6; oi++) {
        int idx = order[oi];
        const auto& ae = axisEnds[idx];
        ImVec2 endPt = project(ae.dir);
        ImVec2 center2d = ImVec2(cx, cy);

        float d = depthOf(ae.dir);
        float alpha = d > 0 ? 1.0f : 0.4f;

        // Line
        float lineThick = ae.positive ? 2.5f : 1.5f;
        dl->AddLine(center2d, endPt, applyAlpha(lineColors[ae.axisIdx], alpha), lineThick);

        // Hit test
        float hitR = ae.positive ? 14.0f : 9.0f;
        float ddx = mousePos.x - endPt.x;
        float ddy = mousePos.y - endPt.y;
        bool hover = (ddx*ddx + ddy*ddy) < hitR * hitR;

        ImU32 dotCol = applyAlpha(hover ? ae.colorHover : ae.color, alpha);

        if (ae.positive) {
            // Larger dot with axis letter
            float dotR = 12.0f;
            dl->AddCircleFilled(endPt, dotR, dotCol, 20);
            if (hover) dl->AddCircle(endPt, dotR + 2, IM_COL32(255, 255, 255, (int)(180*alpha)), 20, 1.5f);

            // Axis letter centered
            ImVec2 textSize = ImGui::CalcTextSize(ae.axisChar);
            dl->AddText(ImVec2(endPt.x - textSize.x*0.5f, endPt.y - textSize.y*0.5f),
                        IM_COL32(255,255,255, (int)(240*alpha)), ae.axisChar);

            // View name tooltip on hover
            if (hover && d > -0.5f) {
                ImVec2 nameSize = ImGui::CalcTextSize(ae.viewName);
                float tipX = endPt.x - nameSize.x * 0.5f;
                float tipY = endPt.y - 12.0f - nameSize.y - 4;
                dl->AddRectFilled(ImVec2(tipX - 4, tipY - 2), ImVec2(tipX + nameSize.x + 4, tipY + nameSize.y + 2),
                                  IM_COL32(20, 20, 25, 220), 4.0f);
                dl->AddText(ImVec2(tipX, tipY), IM_COL32(255,255,255,230), ae.viewName);
            }
        } else {
            // Smaller dot for negative axis
            float dotR = 7.0f;
            dl->AddCircleFilled(endPt, dotR, dotCol, 16);
            if (hover) {
                dl->AddCircle(endPt, dotR + 2, IM_COL32(255,255,255, (int)(140*alpha)), 16, 1.5f);
                // Show view name tooltip
                ImVec2 nameSize = ImGui::CalcTextSize(ae.viewName);
                float tipX = endPt.x - nameSize.x * 0.5f;
                float tipY = endPt.y - 7.0f - nameSize.y - 4;
                dl->AddRectFilled(ImVec2(tipX - 4, tipY - 2), ImVec2(tipX + nameSize.x + 4, tipY + nameSize.y + 2),
                                  IM_COL32(20, 20, 25, 220), 4.0f);
                dl->AddText(ImVec2(tipX, tipY), IM_COL32(255,255,255,220), ae.viewName);
            }
        }

        if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            animateCameraTo(ae.yaw, ae.pitch, app.camera.target);
        }
    }
}
