#include "tool_move.h"
#include "undo.h"

#include <imgui.h>
#include <cmath>
#include <vector>

using namespace cad;

namespace {

const float kAxisLenPx  = 90.0f;   // gizmo arm length, in screen pixels
const float kGrabPx     = 10.0f;   // how close the cursor must be to grab

Vec3 axisDir(int axis) {
    return axis == 0 ? Vec3{1, 0, 0} : axis == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
}

ImU32 axisColour(int axis, bool hot) {
    if (hot) return IM_COL32(255, 225, 90, 255);
    switch (axis) {
        case 0:  return IM_COL32(232, 88, 88, 255);
        case 1:  return IM_COL32(120, 210, 120, 255);
        default: return IM_COL32(96, 150, 245, 255);
    }
}

// Every body in the current selection, deduplicated.
std::vector<int> selectedBodies() {
    std::vector<int> out;
    for (int i : app.selectedSolidSet)
        if (i >= 0 && i < (int)app.solids.size()) out.push_back(i);
    if (out.empty() && app.selectedSolidIdx >= 0 &&
        app.selectedSolidIdx < (int)app.solids.size())
        out.push_back(app.selectedSolidIdx);
    return out;
}

Vec2 project(Vec3 p, bool& ok) {
    Mat4 vp = app.camera.projMatrix() * app.camera.viewMatrix();
    Vec4 clip = vp * Vec4(p, 1.0f);
    if (clip.w <= 0.0001f) { ok = false; return {0, 0}; }
    ok = true;
    return { (clip.x / clip.w * 0.5f + 0.5f) * (float)app.windowWidth,
             (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * (float)app.windowHeight };
}

// World length that projects to `px` screen pixels at the gizmo's depth, so the
// gizmo keeps a constant on-screen size at any zoom.
float worldPerPixel() {
    float tanHalf = std::tan(app.camera.fov * DEG2RAD * 0.5f);
    return (2.0f * app.camera.distance * tanHalf) /
           (float)std::max(1, app.camera.viewportHeight);
}

// Mouse position in the same (logical window) space the projection produces.
Vec2 mouseLogical() {
    float sx = (app.camera.viewportWidth > 0)
        ? (float)app.windowWidth / (float)app.camera.viewportWidth : 1.0f;
    float sy = (app.camera.viewportHeight > 0)
        ? (float)app.windowHeight / (float)app.camera.viewportHeight : 1.0f;
    return { app.mouseX * sx, app.mouseY * sy };
}

// How far along `axis` the cursor currently sits, measured by projecting the
// cursor onto the axis in screen space.  Using screen space keeps the drag
// stable even when the axis points nearly at the camera.
bool axisParam(int axis, float& out) {
    bool okA = false, okB = false;
    Vec2 a = project(app.moveGizmoOrigin, okA);
    float armWorld = kAxisLenPx * worldPerPixel();
    Vec2 b = project(app.moveGizmoOrigin + axisDir(axis) * armWorld, okB);
    if (!okA || !okB) return false;

    Vec2 ab = b - a;
    float len2 = ab.lengthSq();
    if (len2 < 1.0f) return false;

    Vec2 m = mouseLogical();
    float t = (m - a).dot(ab) / len2;      // 0 at origin, 1 at the arm tip
    out = t * armWorld;                    // convert back to world units
    return true;
}

} // namespace

void updateMoveGizmo() {
    auto bodies = selectedBodies();
    if (bodies.empty()) { app.moveGizmoOrigin = {0, 0, 0}; return; }
    AABB bb;
    for (int i : bodies) {
        AABB b = app.solids[i].bounds();
        bb.expand(b.min);
        bb.expand(b.max);
    }
    app.moveGizmoOrigin = bb.center();
}

void drawMoveGizmo() {
    if (app.currentTool != Tool::Move) return;
    if (selectedBodies().empty()) return;
    if (!app.moveDragging) updateMoveGizmo();

    bool ok = false;
    Vec2 originS = project(app.moveGizmoOrigin, ok);
    if (!ok) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float armWorld = kAxisLenPx * worldPerPixel();

    // Pick the closest axis under the cursor, unless a drag already owns one.
    if (!app.moveDragging) {
        app.moveHoverAxis = -1;
        float best = kGrabPx;
        Vec2 m = mouseLogical();
        for (int a = 0; a < 3; a++) {
            bool okT = false;
            Vec2 tip = project(app.moveGizmoOrigin + axisDir(a) * armWorld, okT);
            if (!okT) continue;
            Vec2 ab = tip - originS;
            float len2 = ab.lengthSq();
            if (len2 < 1.0f) continue;
            float t = std::max(0.0f, std::min(1.0f, (m - originS).dot(ab) / len2));
            float d = m.distTo(originS + ab * t);
            if (d < best) { best = d; app.moveHoverAxis = a; }
        }
    }

    static const char* kLabels[3] = { "X", "Y", "Z" };
    for (int a = 0; a < 3; a++) {
        bool okT = false;
        Vec2 tip = project(app.moveGizmoOrigin + axisDir(a) * armWorld, okT);
        if (!okT) continue;
        bool hot = (app.moveDragging ? app.moveAxis : app.moveHoverAxis) == a;
        ImU32 col = axisColour(a, hot);

        ImVec2 o(originS.x, originS.y), t(tip.x, tip.y);
        dl->AddLine(o, t, col, hot ? 3.5f : 2.5f);

        // Arrow head
        Vec2 dir = (tip - originS);
        float l = dir.length();
        if (l > 1.0f) {
            dir = dir * (1.0f / l);
            Vec2 perp{-dir.y, dir.x};
            float hs = hot ? 8.0f : 6.5f;
            Vec2 p1 = tip - dir * (hs * 2.0f) + perp * hs;
            Vec2 p2 = tip - dir * (hs * 2.0f) - perp * hs;
            dl->AddTriangleFilled(t, ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), col);
            dl->AddText(ImVec2(tip.x + dir.x * 12.0f - 4.0f, tip.y + dir.y * 12.0f - 7.0f),
                        col, kLabels[a]);
        }
    }
    dl->AddCircleFilled(ImVec2(originS.x, originS.y), 4.5f, IM_COL32(235, 235, 240, 235), 12);

    // Live readout while dragging.
    if (app.moveDragging) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s  %+.2f mm",
                      kLabels[app.moveAxis],
                      app.moveAccumulated.x + app.moveAccumulated.y + app.moveAccumulated.z);
        ImVec2 at(originS.x + 16.0f, originS.y - 26.0f);
        ImVec2 sz = ImGui::CalcTextSize(buf);
        dl->AddRectFilled(ImVec2(at.x - 6, at.y - 4),
                          ImVec2(at.x + sz.x + 6, at.y + sz.y + 4),
                          IM_COL32(24, 26, 32, 230), 4.0f);
        dl->AddText(at, IM_COL32(240, 240, 245, 255), buf);
    }
}

bool handleMoveMouseDown() {
    if (app.currentTool != Tool::Move) return false;
    if (app.moveHoverAxis < 0) return false;
    if (selectedBodies().empty()) return false;

    float v = 0;
    if (!axisParam(app.moveHoverAxis, v)) return false;

    app.moveAxis = app.moveHoverAxis;
    app.moveDragging = true;
    app.moveDragStartValue = v;
    app.moveAccumulated = {0, 0, 0};
    app.moveStartCentroid = app.moveGizmoOrigin;
    undoStack.pushState("Move");
    app.statusText = "Dragging — release to place";
    return true;
}

void handleMoveDrag() {
    if (!app.moveDragging || app.moveAxis < 0) return;

    float v = 0;
    if (!axisParam(app.moveAxis, v)) return;

    float total = v - app.moveDragStartValue;
    float already = app.moveAccumulated.x + app.moveAccumulated.y + app.moveAccumulated.z;
    float step = total - already;
    if (std::abs(step) < 1e-6f) return;

    Vec3 delta = axisDir(app.moveAxis) * step;
    for (int i : selectedBodies()) translateSolid(app.solids[i], delta);

    app.moveAccumulated += delta;
    app.moveGizmoOrigin += delta;
    app.statusText = "Moving...";
}

void handleMoveRelease() {
    if (!app.moveDragging) return;
    app.moveDragging = false;
    app.moveAxis = -1;
    float d = app.moveAccumulated.x + app.moveAccumulated.y + app.moveAccumulated.z;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Moved %+.2f mm", d);
    app.statusText = buf;
    updateMoveGizmo();
}

void moveSelectionBy(Vec3 delta, const char* undoLabel) {
    auto bodies = selectedBodies();
    if (bodies.empty()) return;
    if (delta.lengthSq() < 1e-12f) return;
    undoStack.pushState(undoLabel);
    for (int i : bodies) translateSolid(app.solids[i], delta);
    updateMoveGizmo();
}

// ---------------------------------------------------------------------------
// Construction plane offset handle
// ---------------------------------------------------------------------------

namespace {

ConstructionPlaneData* activePlane() {
    int i = app.selectedConstructionPlaneIdx;
    if (i < 0 || i >= (int)app.constructionPlanes.size()) return nullptr;
    return &app.constructionPlanes[i];
}

const float kPlaneGrabPx = 15.0f;   // generous: missing this used to be costly

// True when the cursor is over the offset arrow.  Computed here rather than
// read from a flag set during rendering, so a click cannot act on a hover
// state that is a frame out of date.
bool planeHandleHit(const ConstructionPlaneData& cp) {
    bool okA = false, okB = false;
    Vec2 a = project(cp.origin, okA);
    float arm = kAxisLenPx * worldPerPixel();
    Vec2 b = project(cp.origin + cp.normal * (arm * 0.55f), okB);
    if (!okA || !okB) return false;

    Vec2 ab = b - a;
    float len2 = ab.lengthSq();
    if (len2 < 1.0f) return false;

    Vec2 m = mouseLogical();
    float t = std::max(0.0f, std::min(1.0f, (m - a).dot(ab) / len2));
    return m.distTo(a + ab * t) < kPlaneGrabPx;
}

// Distance along the plane normal that the cursor currently projects to.
bool planeParam(const ConstructionPlaneData& cp, float& out) {
    bool okA = false, okB = false;
    Vec2 a = project(cp.baseOrigin, okA);
    float arm = kAxisLenPx * worldPerPixel();
    Vec2 b = project(cp.baseOrigin + cp.normal * arm, okB);
    if (!okA || !okB) return false;
    Vec2 ab = b - a;
    float len2 = ab.lengthSq();
    if (len2 < 1.0f) return false;
    Vec2 m = mouseLogical();
    out = ((m - a).dot(ab) / len2) * arm;
    return true;
}

} // namespace

void drawPlaneHandle() {
    if (app.currentTool != Tool::ConstructionPlane) return;
    ConstructionPlaneData* cp = activePlane();
    if (!cp) return;

    bool ok = false;
    Vec2 originS = project(cp->origin, ok);
    if (!ok) return;

    float arm = kAxisLenPx * worldPerPixel();
    bool okT = false;
    Vec2 tip = project(cp->origin + cp->normal * (arm * 0.55f), okT);
    if (!okT) return;

    if (!app.planeDragging) app.planeHandleHovered = planeHandleHit(*cp);

    bool hot = app.planeDragging || app.planeHandleHovered;
    ImU32 col = hot ? IM_COL32(255, 225, 90, 255) : IM_COL32(120, 190, 255, 235);
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    dl->AddLine(ImVec2(originS.x, originS.y), ImVec2(tip.x, tip.y), col, hot ? 3.5f : 2.5f);
    Vec2 dir = tip - originS;
    float l = dir.length();
    if (l > 1.0f) {
        dir = dir * (1.0f / l);
        Vec2 perp{-dir.y, dir.x};
        float hs = hot ? 8.0f : 6.5f;
        Vec2 p1 = tip - dir * (hs * 2.0f) + perp * hs;
        Vec2 p2 = tip - dir * (hs * 2.0f) - perp * hs;
        dl->AddTriangleFilled(ImVec2(tip.x, tip.y), ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), col);
    }
    dl->AddCircleFilled(ImVec2(originS.x, originS.y), 4.0f, IM_COL32(235, 235, 240, 230), 12);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "offset %+.2f mm", cp->offset);
    ImVec2 at(originS.x + 14.0f, originS.y - 24.0f);
    ImVec2 sz = ImGui::CalcTextSize(buf);
    dl->AddRectFilled(ImVec2(at.x - 6, at.y - 4), ImVec2(at.x + sz.x + 6, at.y + sz.y + 4),
                      IM_COL32(24, 26, 32, 230), 4.0f);
    dl->AddText(at, IM_COL32(240, 240, 245, 255), buf);
}

bool handlePlaneMouseDown() {
    if (app.currentTool != Tool::ConstructionPlane) return false;
    ConstructionPlaneData* cp = activePlane();
    if (!cp) return false;
    if (!planeHandleHit(*cp)) return false;
    app.planeHandleHovered = true;

    float v = 0;
    if (!planeParam(*cp, v)) return false;

    undoStack.pushState("Move plane");
    app.planeDragging = true;
    app.planeDragStartValue = v;
    app.planeDragStartOffset = cp->offset;
    app.statusText = "Sliding plane — release to place";
    return true;
}

void handlePlaneDrag() {
    if (!app.planeDragging) return;
    ConstructionPlaneData* cp = activePlane();
    if (!cp) { app.planeDragging = false; return; }

    float v = 0;
    if (!planeParam(*cp, v)) return;
    cp->offset = app.planeDragStartOffset + (v - app.planeDragStartValue);
    cp->refresh();
    app.statusText = "Plane offset";
}

void handlePlaneRelease() {
    if (!app.planeDragging) return;
    app.planeDragging = false;
    ConstructionPlaneData* cp = activePlane();
    if (cp) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Plane offset %+.2f mm", cp->offset);
        app.statusText = buf;
    }
}
