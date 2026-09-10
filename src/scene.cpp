#include "scene.h"
#include "app_state.h"
#include "input.h"
#include "extrude.h"
#include "picking.h"
#include "ui.h"
#include "tool_move.h"

#include <GLFW/glfw3.h>
#ifdef __APPLE__
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif
#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace cad;

// Forward declarations for marquee helpers (defined later in this file).
static bool marqueeIsArmable();
static void commitMarqueeSelection();
static void drawMarqueeRect();

// ---- Input processing (mouse, camera, sketch cursor) ----
void processInput(GLFWwindow* window) {
    ImGuiIO& io = ImGui::GetIO();

    int fbWidth, fbHeight;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    app.camera.viewportWidth = fbWidth;
    app.camera.viewportHeight = fbHeight;

    int winWidth, winHeight;
    glfwGetWindowSize(window, &winWidth, &winHeight);
    app.windowWidth = winWidth;
    app.windowHeight = winHeight;

    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    float xscale = (float)fbWidth / (float)winWidth;
    float yscale = (float)fbHeight / (float)winHeight;

    app.lastMouseX = app.mouseX;
    app.lastMouseY = app.mouseY;
    app.mouseX = (float)mx * xscale;
    app.mouseY = (float)my * yscale;
    float dx = app.mouseX - app.lastMouseX;
    float dy = app.mouseY - app.lastMouseY;

    bool middleNow = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
    bool leftNow = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    bool rightNow = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    app.shiftDown = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                     glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    app.altDown = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                  glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;

    // Navigation drag: Option+left-drag is the laptop path (a trackpad has no
    // middle button, and right-drag is taken by the context menu), while
    // middle/right-drag stays available for anyone on a real mouse.
    bool navDragging = middleNow || rightNow || (leftNow && app.altDown);
    app.cameraNavigating = navDragging;

    // Track whether this right-press turned into a drag (an orbit) so the
    // context menu can tell a click apart from a camera move.
    if (rightNow && !app.mouseRight) {
        app.rightDownX = app.mouseX;
        app.rightDownY = app.mouseY;
        app.rightDragMoved = false;
    } else if (rightNow) {
        float rdx = app.mouseX - app.rightDownX;
        float rdy = app.mouseY - app.rightDownY;
        if (rdx * rdx + rdy * rdy > 16.0f) app.rightDragMoved = true;  // ~4 px
    }

    if (!io.WantCaptureMouse) {
        if (navDragging && app.shiftDown) {
            app.camera.pan(dx, dy);
        } else if (navDragging) {
            app.camera.orbit(dx, dy);
        }

        if (app.moveDragging && leftNow) {
            handleMoveDrag();
        }
        if (app.moveDragging && !leftNow && app.mouseLeft) {
            handleMoveRelease();
        }

        if (app.planeDragging && leftNow) {
            handlePlaneDrag();
        }
        if (app.planeDragging && !leftNow && app.mouseLeft) {
            handlePlaneRelease();
        }

        if (app.extrudeDragging && leftNow) {
            handleExtrudeDrag(dy);
        }
        if (app.extrudeDragging && !leftNow && app.mouseLeft) {
            handleExtrudeRelease();
        }

        if (app.edgeModifierDragging && leftNow &&
            (app.currentTool == Tool::Chamfer || app.currentTool == Tool::Fillet)) {
            Vec2 base = app.edgeModifierGizmoBaseScreen;
            Vec2 tip = app.edgeModifierGizmoTipScreen;
            Vec2 axis = tip - base;
            float axisLen = axis.length();
            if (axisLen < 1.0f) axis = {0.0f, -1.0f};
            else axis = axis * (1.0f / axisLen);

            Vec2 mouseDelta = {app.mouseX - app.edgeModifierDragStartX,
                               app.mouseY - app.edgeModifierDragStartY};
            float projected = mouseDelta.dot(axis);

            float scale = std::max(0.003f, app.camera.distance * 0.003f);
            float value = app.edgeModifierDragStartValue + projected * scale;
            if (app.currentTool == Tool::Chamfer) app.chamferDistance = value;
            else app.filletRadius = value;
            
            // Update live preview
            updateEdgeModifierPreview();
        }
        if (app.edgeModifierDragging && !leftNow && app.mouseLeft) {
            app.edgeModifierDragging = false;
            app.statusText = "Adjust value or press Enter to apply";
        }

        // ---- Marquee (drag-rectangle) selection ----
        // Arm on left-press over empty viewport in tools that support it.
        if (leftNow && !app.mouseLeft && !app.altDown &&
            !app.extrudeDragging && !app.edgeModifierDragging &&
            !app.moveDragging && !app.planeDragging &&
            marqueeIsArmable()) {
            app.marqueePending = true;
            app.marqueeActive = false;
            app.marqueeStartX = app.mouseX;
            app.marqueeStartY = app.mouseY;
        }
        // Promote pending -> active once the cursor moves past a small threshold.
        if (leftNow && app.marqueePending && !app.marqueeActive) {
            float ddx = app.mouseX - app.marqueeStartX;
            float ddy = app.mouseY - app.marqueeStartY;
            if (ddx * ddx + ddy * ddy > 25.0f) {  // ~5 px
                app.marqueeActive = true;
            }
        }
        // On release, commit the marquee or fall through to the click handler.
        if (!leftNow && app.mouseLeft) {
            if (app.marqueeActive) {
                commitMarqueeSelection();
                app.marqueeActive = false;
                app.marqueePending = false;
                // Suppress the click that would otherwise fire below.
                app.mouseLeft = leftNow;
                app.mouseMiddle = middleNow;
                app.mouseRight = rightNow;
                return;
            }
            app.marqueePending = false;
        }

        if (leftNow && !app.mouseLeft && !app.altDown &&
            !app.extrudeDragging && !app.edgeModifierDragging &&
            !app.moveDragging && !app.planeDragging) {
            // Grabbing a manipulator handle takes priority over selecting.
            if (!handleMoveMouseDown() && !handlePlaneMouseDown()) handleMouseClick();
        }
    }

    app.mouseMiddle = middleNow;
    app.mouseLeft = leftNow;
    app.mouseRight = rightNow;

    // Keep the snap radius constant on screen rather than constant in
    // millimetres.  A fixed 0.15 mm tolerance is a sub-pixel target on a
    // typical part, which is why nothing felt magnetic.
    if (app.inSketchMode) {
        float tanHalf = std::tan(app.camera.fov * DEG2RAD * 0.5f);
        float worldPerPixel = (2.0f * app.camera.distance * tanHalf) /
                              (float)std::max(1, app.camera.viewportHeight);
        app.sketch.snapDistance = std::max(0.02f, worldPerPixel * 20.0f);
    }

    // Update sketch cursor
    if (app.inSketchMode && app.sketchDrawing) {
        Ray ray = app.camera.screenToRay(app.mouseX, app.mouseY);
        float t = app.sketch.sketchPlane.intersectRay(ray);
        if (t > 0) {
            Vec3 worldHit = ray.at(t);
            Vec2 sketchPos = app.sketch.worldToSketch(worldHit);
            SnapResult snap = app.sketch.findSnap(sketchPos, true, app.sketchDrawStart);
            app.sketchDrawCurrent = snap.valid() ? snap.point : sketchPos;
            app.currentSnap = snap;

            Vec2 delta = app.sketchDrawCurrent - app.sketchDrawStart;
            if (app.currentTool == Tool::SketchRect) {
                app.dimInput.liveLength = delta.x;
                app.dimInput.liveAngle = delta.y;
            } else {
                app.dimInput.liveLength = delta.length();
                app.dimInput.liveAngle = std::atan2(delta.y, delta.x) * RAD2DEG;
            }

            if (app.currentTool == Tool::SketchRect) {
                float w = app.dimInput.lengthSet ? app.dimInput.length : delta.x;
                float h = app.dimInput.angleSet ? app.dimInput.angle : delta.y;
                app.sketchDrawCurrent = app.sketchDrawStart + Vec2{w, h};
            }

            if (app.dimInput.lengthSet && app.currentTool == Tool::SketchLine) {
                Vec2 delta2 = app.sketchDrawCurrent - app.sketchDrawStart;
                float mouseAngle = std::atan2(delta2.y, delta2.x) * RAD2DEG;
                float ang = app.dimInput.angleSet ? app.dimInput.angle : mouseAngle;
                app.sketchDrawCurrent = computeEndpoint(app.sketchDrawStart, app.dimInput.length, ang);
            }
        }
    } else if (app.inSketchMode && !app.sketchDrawing &&
               (app.currentTool == Tool::SketchLine ||
                app.currentTool == Tool::SketchCircle ||
                app.currentTool == Tool::SketchRect)) {
        Ray ray = app.camera.screenToRay(app.mouseX, app.mouseY);
        float t = app.sketch.sketchPlane.intersectRay(ray);
        if (t > 0) {
            Vec3 worldHit = ray.at(t);
            Vec2 sketchPos = app.sketch.worldToSketch(worldHit);
            SnapResult snap = app.sketch.findSnap(sketchPos);
            app.currentSnap = snap;
        } else {
            app.currentSnap = {};
        }
    }

    updateOriginCubeHover();
    updatePicking();
}

// ---- Draw extrude arrow overlay ----
static void drawOneArrow(ImVec2 baseScreen, Vec3 base, Vec3 arrowDir, float dist,
                          ImDrawList* dl, const Mat4& vp, ImU32 col, ImU32 colFill,
                          bool drawDistanceLabel = true) {
    auto project = [&](Vec3 p) -> ImVec2 {
        Vec4 clip = vp * Vec4(p, 1.0f);
        if (clip.w <= 0) return ImVec2(-1, -1);
        float sx = (clip.x / clip.w * 0.5f + 0.5f) * app.windowWidth;
        float sy = (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * app.windowHeight;
        return ImVec2(sx, sy);
    };

    // Compute tip: use actual distance (can be negative)
    float absDist = std::abs(dist);
    float signDist = (dist < 0.0f) ? -1.0f : 1.0f;
    Vec3 tip = base + arrowDir * dist;
    ImVec2 tipScreen = project(tip);

    float adx = tipScreen.x - baseScreen.x;
    float ady = tipScreen.y - baseScreen.y;
    float arrowLen = std::sqrt(adx * adx + ady * ady);

    float minArrowLen = 40.0f;
    ImVec2 arrowTip = tipScreen;
    if (arrowLen < minArrowLen && arrowLen > 0.1f) {
        float scale = minArrowLen / arrowLen;
        arrowTip = ImVec2(baseScreen.x + adx * scale, baseScreen.y + ady * scale);
        arrowLen = minArrowLen;
    } else if (arrowLen <= 0.1f) {
        Vec3 guideEnd = base + arrowDir * signDist * (app.camera.distance * 0.1f);
        arrowTip = project(guideEnd);
        adx = arrowTip.x - baseScreen.x;
        ady = arrowTip.y - baseScreen.y;
        arrowLen = std::sqrt(adx * adx + ady * ady);
        if (arrowLen < minArrowLen) {
            float scale = minArrowLen / arrowLen;
            arrowTip = ImVec2(baseScreen.x + adx * scale, baseScreen.y + ady * scale);
            arrowLen = minArrowLen;
        }
    }

    float ndx = (arrowTip.x - baseScreen.x) / arrowLen;
    float ndy = (arrowTip.y - baseScreen.y) / arrowLen;
    float px = -ndy, py = ndx;

    dl->AddLine(baseScreen, arrowTip, col, 3.0f);

    float headLen = 14.0f;
    float headW = 8.0f;
    ImVec2 headBase(arrowTip.x - ndx * headLen, arrowTip.y - ndy * headLen);
    ImVec2 headL(headBase.x + px * headW, headBase.y + py * headW);
    ImVec2 headR(headBase.x - px * headW, headBase.y - py * headW);
    dl->AddTriangleFilled(arrowTip, headL, headR, colFill);
    dl->AddTriangle(arrowTip, headL, headR, col, 2.0f);

    if (drawDistanceLabel && absDist > 0.01f) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f mm", dist);
        ImVec2 textSize = ImGui::CalcTextSize(buf);
        float lx = arrowTip.x + px * 16.0f - textSize.x * 0.5f;
        float ly = arrowTip.y + py * 16.0f - textSize.y * 0.5f;
        dl->AddRectFilled(ImVec2(lx - 4, ly - 2),
            ImVec2(lx + textSize.x + 4, ly + textSize.y + 2),
            IM_COL32(30, 30, 35, 220), 4.0f);
        dl->AddText(ImVec2(lx, ly), IM_COL32(255, 200, 80, 255), buf);
    }
}

static void drawExtrudeArrow() {
    if (!(app.currentTool == Tool::Extrude && app.extrudePreviewing &&
          (!app.extrudeProfiles.empty() || app.extrudeSelectedProfile == -2)))
        return;

    Vec3 base = app.extrudeArrowBase;
    Vec3 dir = app.extrudeArrowDir;

    Mat4 vp = app.camera.projMatrix() * app.camera.viewMatrix();
    auto project = [&](Vec3 p) -> ImVec2 {
        Vec4 clip = vp * Vec4(p, 1.0f);
        if (clip.w <= 0) return ImVec2(-1, -1);
        float sx = (clip.x / clip.w * 0.5f + 0.5f) * app.windowWidth;
        float sy = (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * app.windowHeight;
        return ImVec2(sx, sy);
    };

    ImVec2 baseScreen = project(base);
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    ImU32 arrowCol = app.extrudeDragging ? IM_COL32(255, 200, 50, 255)
                                          : IM_COL32(255, 160, 30, 255);
    ImU32 arrowColFill = app.extrudeDragging ? IM_COL32(255, 200, 50, 180)
                                              : IM_COL32(255, 160, 30, 140);

    // Base circle (shared)
    float baseR = 6.0f;
    dl->AddCircleFilled(baseScreen, baseR, arrowColFill, 16);
    dl->AddCircle(baseScreen, baseR, arrowCol, 16, 2.0f);

    // Primary arrow
    drawOneArrow(baseScreen, base, dir, app.extrudeDistance, dl, vp, arrowCol, arrowColFill);

    // Second arrow for Two Sides mode
    if (app.extrudeDir == ExtrudeDirection::TwoSides) {
        ImU32 col2 = IM_COL32(100, 180, 255, 255);
        ImU32 colFill2 = IM_COL32(100, 180, 255, 140);
        drawOneArrow(baseScreen, base, -dir, app.extrudeDistance2, dl, vp, col2, colFill2);
    }
}

static void drawEdgeModifierGizmo() {
    app.edgeModifierGizmoVisible = false;
    app.edgeModifierChipVisible = false;

    if (!(app.currentTool == Tool::Chamfer || app.currentTool == Tool::Fillet)) return;
    if (app.edgeModifierTargetSolidIdx < 0 ||
        app.edgeModifierTargetSolidIdx >= (int)app.solids.size() ||
        app.selectedEdgeSet.empty()) {
        return;
    }

    // Skip gizmo drawing while actively dragging to avoid double-arrow issues
    if (app.edgeModifierDragging) {
        return;
    }

    // Only recalculate gizmo position when not cached
    if (!app.edgeModifierGizmoCached) {
        // Use backup solid for gizmo calculations when previewing, to avoid edge geometry changes
        const auto& solid = app.edgeModifierPreviewing ? app.edgeModifierBackupSolid : app.solids[app.edgeModifierTargetSolidIdx];
        Vec3 base{0, 0, 0};
        Vec3 dir{0, 0, 0};
        int count = 0;

        for (int ei : app.selectedEdgeSet) {
            if (ei < 0 || ei >= (int)solid.edges.size()) continue;
            const auto& edge = solid.edges[ei];
            base += edge.midpoint();

            Vec3 n{0,0,0};
            if (edge.face0 != NULL_ID && edge.face0 < solid.faces.size()) n += solid.faces[edge.face0].normal;
            if (edge.face1 != NULL_ID && edge.face1 < solid.faces.size()) n += solid.faces[edge.face1].normal;
            if (n.lengthSq() < EPSILON) n = app.camera.getUpDir();
            dir += n.normalized();
            count++;
        }

        if (count == 0) return;

        base = base * (1.0f / (float)count);
        if (dir.lengthSq() < EPSILON) dir = app.camera.getUpDir();
        dir = dir.normalized();

        app.edgeModifierGizmoBase = base;
        app.edgeModifierGizmoDir = dir;
    }

    app.edgeModifierGizmoVisible = true;

    float dist = (app.currentTool == Tool::Chamfer) ? app.chamferDistance : app.filletRadius;
    Vec3 base = app.edgeModifierGizmoBase;
    Vec3 dir = app.edgeModifierGizmoDir;

    Mat4 vp = app.camera.projMatrix() * app.camera.viewMatrix();
    auto project = [&](Vec3 p) -> ImVec2 {
        Vec4 clip = vp * Vec4(p, 1.0f);
        if (clip.w <= 0) return ImVec2(-1, -1);
        float sx = (clip.x / clip.w * 0.5f + 0.5f) * app.windowWidth;
        float sy = (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * app.windowHeight;
        return ImVec2(sx, sy);
    };

    ImVec2 baseScreen = project(base);
    ImVec2 tipScreen = project(base + dir * dist);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImU32 col = IM_COL32(255, 165, 0, 255);
    ImU32 colFill = IM_COL32(255, 165, 0, 150);
    drawOneArrow(baseScreen, base, dir, dist, dl, vp, col, colFill, false);

    app.edgeModifierGizmoBaseScreen = {baseScreen.x, baseScreen.y};
    app.edgeModifierGizmoTipScreen = {tipScreen.x, tipScreen.y};

    if (tipScreen.x >= 0 && tipScreen.y >= 0) {
        char label[64];
        if (app.currentTool == Tool::Chamfer) {
            std::snprintf(label, sizeof(label), "Chamfer %.2f mm", app.chamferDistance);
        } else {
            std::snprintf(label, sizeof(label), "Fillet R %.2f mm", app.filletRadius);
        }

        ImVec2 textSize = ImGui::CalcTextSize(label);
        float lx = tipScreen.x + 12.0f;
        float ly = tipScreen.y - textSize.y - 8.0f;

        ImU32 bg = app.edgeModifierDragging
            ? IM_COL32(36, 28, 8, 235)
            : IM_COL32(26, 28, 35, 215);
        ImU32 fg = app.edgeModifierDragging
            ? IM_COL32(255, 220, 120, 255)
            : IM_COL32(255, 190, 80, 255);

        dl->AddRectFilled(ImVec2(lx - 5, ly - 3),
                          ImVec2(lx + textSize.x + 5, ly + textSize.y + 3),
                          bg, 5.0f);
        dl->AddText(ImVec2(lx, ly), fg, label);

        app.edgeModifierChipVisible = true;
        app.edgeModifierChipX0 = lx - 5;
        app.edgeModifierChipY0 = ly - 3;
        app.edgeModifierChipX1 = lx + textSize.x + 5;
        app.edgeModifierChipY1 = ly + textSize.y + 3;

        if (app.edgeModifierInputActive) {
            ImGui::SetNextWindowPos(ImVec2(app.edgeModifierChipX0, app.edgeModifierChipY0), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2((app.edgeModifierChipX1 - app.edgeModifierChipX0), 34), ImGuiCond_Always);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.1f, 0.13f, 0.96f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
            ImGui::Begin("##EdgeModifierInput", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

            if (app.edgeModifierInputFocus) {
                ImGui::SetKeyboardFocusHere();
                app.edgeModifierInputFocus = false;
            }
            ImGui::SetNextItemWidth(-1);
            bool submit = ImGui::InputText("##edge-modifier-value", app.edgeModifierInputBuf,
                sizeof(app.edgeModifierInputBuf),
                ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);

            if (submit) {
                float v = (float)std::atof(app.edgeModifierInputBuf);
                if (app.currentTool == Tool::Chamfer) app.chamferDistance = v;
                else if (app.currentTool == Tool::Fillet) app.filletRadius = v;
                app.edgeModifierInputActive = false;
            }

            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                app.edgeModifierInputActive = false;
            }

            ImGui::End();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
    }
}

// ---- Marquee selection ----
//
// Marquee selection lives in the viewport for the Select / Chamfer / Fillet
// tools.  It only arms when the user presses left-mouse on empty space (no
// face/edge/corner under the cursor) and not while ImGui owns the mouse, so
// it never competes with normal click selection or with extrude / edge-modifier
// drag gizmos.
//
// What gets selected depends on the active tool:
//   * Chamfer / Fillet -> edges (each edge picked also expands to its curve
//     group so a fillet rim selects all of its straight segments).
//   * Select           -> faces and the solids that contain them.

static bool marqueeIsArmable() {
    // Only in tools whose marquee semantics we actually defined.
    if (app.currentTool != Tool::Select &&
        app.currentTool != Tool::Move &&
        app.currentTool != Tool::Chamfer &&
        app.currentTool != Tool::Fillet) {
        return false;
    }
    // Don't arm while another mouse interaction is brewing.
    if (app.inSketchMode) return false;
    if (app.extrudePreviewing || app.extrudeDragging) return false;
    if (app.planePick == PlanePickMode::WaitingForPlane) return false;

    // Require an empty viewport hit so single-click selection is not lost.
    bool hitSomething = (app.hoveredFaceIdx >= 0) ||
                        (app.hoveredEdgeIdx >= 0) ||
                        (app.hoveredCornerValid) ||
                        (app.hoveredOriginPlane >= 0);
    return !hitSomething;
}

static inline ImVec2 projectToScreen(const Mat4& vp, Vec3 p) {
    Vec4 clip = vp * Vec4(p, 1.0f);
    if (clip.w <= 0) return ImVec2(-1, -1);  // behind camera
    float sx = (clip.x / clip.w * 0.5f + 0.5f) * app.windowWidth;
    float sy = (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * app.windowHeight;
    return ImVec2(sx, sy);
}

static void commitMarqueeSelection() {
    float x0 = std::min(app.marqueeStartX, app.mouseX);
    float x1 = std::max(app.marqueeStartX, app.mouseX);
    float y0 = std::min(app.marqueeStartY, app.mouseY);
    float y1 = std::max(app.marqueeStartY, app.mouseY);
    float fbToWin = (app.camera.viewportWidth > 0)
        ? (float)app.windowWidth / (float)app.camera.viewportWidth
        : 1.0f;
    // mouseX/Y are in framebuffer pixels; projected ImVec2s are in window
    // pixels.  Convert the rect to window pixels for the screen-space test.
    x0 *= fbToWin; x1 *= fbToWin; y0 *= fbToWin; y1 *= fbToWin;

    auto inRect = [&](ImVec2 p) -> bool {
        if (p.x < 0 && p.y < 0) return false;  // off-screen
        return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1;
    };

    Mat4 vp = app.camera.projMatrix() * app.camera.viewMatrix();

    if (!app.shiftDown) {
        app.selectedSolidSet.clear();
        app.selectedFaceSet.clear();
        app.selectedEdgeSet.clear();
    }

    bool isEdgeMode = (app.currentTool == Tool::Chamfer ||
                       app.currentTool == Tool::Fillet);
    int edgesAdded = 0, facesAdded = 0;

    for (size_t si = 0; si < app.solids.size(); si++) {
        if (si < app.solidVisible.size() && !app.solidVisible[si]) continue;
        const Solid& solid = app.solids[si];

        if (isEdgeMode) {
            // Selecting one body's edges only — match the rest of the chamfer/
            // fillet code which assumes a single target solid.
            if (app.edgeModifierTargetSolidIdx >= 0 &&
                (int)si != app.edgeModifierTargetSolidIdx) continue;

            for (size_t ei = 0; ei < solid.edges.size(); ei++) {
                const auto& e = solid.edges[ei];
                ImVec2 a = projectToScreen(vp, e.start);
                ImVec2 b = projectToScreen(vp, e.end);
                // Accept the edge if either endpoint or its midpoint is inside
                // the rect — this matches the "intersect" feel users expect.
                ImVec2 m((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
                if (inRect(a) || inRect(b) || inRect(m)) {
                    // Expand to the curve group so picking any segment of a
                    // fillet rim brings in the whole rim.
                    auto group = solid.curveGroupEdges((uint32_t)ei);
                    for (uint32_t g : group) {
                        if (app.selectedEdgeSet.insert((int)g).second) edgesAdded++;
                    }
                }
            }
            if (edgesAdded > 0) {
                app.edgeModifierTargetSolidIdx = (int)si;
                app.selectedSolidIdx = (int)si;
            }
        } else {
            // Select tool: marquee adds whole bodies whose AABB centroids fall
            // inside the rectangle (the F360-equivalent of dragging a box over
            // bodies in the viewport).  Fall back to checking face centroids
            // so large bodies still match when the AABB centre is offscreen.
            AABB bb = solid.bounds();
            Vec3 c = (bb.min + bb.max) * 0.5f;
            ImVec2 cs = projectToScreen(vp, c);
            bool bodyHit = inRect(cs);
            if (!bodyHit) {
                for (size_t fi = 0; fi < solid.faces.size(); fi++) {
                    if (solid.faces[fi].outerLoop.empty()) continue;
                    if (inRect(projectToScreen(vp, solid.faces[fi].centroid()))) {
                        bodyHit = true; break;
                    }
                }
            }
            if (bodyHit) {
                if (app.selectedSolidSet.insert((int)si).second) facesAdded++;
                if (app.selectedSolidIdx < 0) app.selectedSolidIdx = (int)si;
            }
        }
    }

    if (isEdgeMode) {
        app.statusText = (edgesAdded > 0)
            ? "Marquee: " + std::to_string(edgesAdded) + " edges added"
            : "Marquee: no edges in rectangle";
    } else {
        app.statusText = (facesAdded > 0)
            ? "Marquee: " + std::to_string(facesAdded) + " bodies added"
            : "Marquee: no bodies in rectangle";
    }
}

static void drawMarqueeRect() {
    if (!app.marqueeActive) return;
    float fbToWin = (app.camera.viewportWidth > 0)
        ? (float)app.windowWidth / (float)app.camera.viewportWidth
        : 1.0f;
    float x0 = std::min(app.marqueeStartX, app.mouseX) * fbToWin;
    float x1 = std::max(app.marqueeStartX, app.mouseX) * fbToWin;
    float y0 = std::min(app.marqueeStartY, app.mouseY) * fbToWin;
    float y1 = std::max(app.marqueeStartY, app.mouseY) * fbToWin;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImU32 fill   = IM_COL32(80, 160, 255, 40);
    ImU32 border = IM_COL32(80, 160, 255, 220);
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), fill);
    dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), border, 0.0f, 0, 1.5f);
}

// ---- Draw profile highlighting ----
static void drawProfileHighlights() {
    if (!(app.sketchVisible && !app.sketch.entities.empty())) return;

    if (app.sketch.entities.size() != app.cachedSketchEntityCount) {
        app.cachedSketchEntityCount = app.sketch.entities.size();
        app.cachedProfiles3D = app.sketch.extractProfiles();
        app.cachedProfiles2D = app.sketch.extractProfiles2D();
    }
    const auto& profiles = app.cachedProfiles3D;

    bool regionPickMode = (app.currentTool == Tool::Extrude &&
                           !app.sketchSourceFaceLoop.empty() &&
                           app.extrudeSelectedProfile == -1);

    if (regionPickMode) {
        int hoverRegion = -1;
        Ray ray = app.camera.screenToRay(app.mouseX, app.mouseY);
        float t = app.sketch.sketchPlane.intersectRay(ray);
        if (t > 0) {
            Vec3 worldHit = ray.at(t);
            Vec2 mouseSketch = app.sketch.worldToSketch(worldHit);
            const auto& profiles2D = app.cachedProfiles2D;
            for (int i = 0; i < (int)profiles2D.size(); i++) {
                if (pointInPolygon2D(mouseSketch, profiles2D[i])) {
                    hoverRegion = i;
                    break;
                }
            }
            if (hoverRegion < 0) {
                std::vector<Vec2> faceBound;
                for (auto& pt : app.sketchSourceFaceLoop)
                    faceBound.push_back(app.sketch.worldToSketch(pt));
                if (pointInPolygon2D(mouseSketch, faceBound))
                    hoverRegion = -2;
            }
        }

        for (int i = 0; i < (int)profiles.size(); i++) {
            if (profiles[i].size() >= 3) {
                Color col = (i == hoverRegion)
                    ? Color{0.2f, 0.8f, 0.3f, 0.35f}
                    : Color{0.3f, 0.6f, 1.0f, 0.2f};
                app.renderer.drawProfileFill(profiles[i], app.camera, col);
            }
        }
        if (hoverRegion == -2 && app.sketchSourceFaceLoop.size() >= 3) {
            app.renderer.drawProfileFill(app.sketchSourceFaceLoop, app.camera,
                {0.8f, 0.5f, 0.2f, 0.25f});
        }
    } else {
        for (auto& profile : profiles) {
            if (profile.size() >= 3) {
                app.renderer.drawProfileFill(profile, app.camera, {0.3f, 0.6f, 1.0f, 0.15f});
            }
        }
    }
}

// ---- Draw sketch preview (in-progress line/circle/rect) ----
static void drawSketchPreview() {
    if (!(app.inSketchMode && app.sketchDrawing)) return;

    Vec3 startW = app.sketch.sketchToWorld(app.sketchDrawStart);
    Vec3 curW = app.sketch.sketchToWorld(app.sketchDrawCurrent);

    // Start point indicator (green square)
    {
        float s = app.camera.distance * 0.006f;
        Vec3 r = app.camera.getRightDir() * s;
        Vec3 u = app.camera.getUpDir() * s;
        glDisable(GL_DEPTH_TEST);
        app.renderer.drawLine3D(startW - r - u, startW + r - u, app.camera, {0, 1, 0.3f, 1}, 2.5f);
        app.renderer.drawLine3D(startW + r - u, startW + r + u, app.camera, {0, 1, 0.3f, 1}, 2.5f);
        app.renderer.drawLine3D(startW + r + u, startW - r + u, app.camera, {0, 1, 0.3f, 1}, 2.5f);
        app.renderer.drawLine3D(startW - r + u, startW - r - u, app.camera, {0, 1, 0.3f, 1}, 2.5f);
        glEnable(GL_DEPTH_TEST);
    }

    glDisable(GL_DEPTH_TEST);
    switch (app.currentTool) {
        case Tool::SketchLine:
            app.renderer.drawLine3D(startW, curW, app.camera, {1, 1, 0, 0.8f}, 1.5f);
            break;
        case Tool::SketchCircle: {
            float r = app.dimInput.lengthSet ? app.dimInput.length
                        : app.sketchDrawStart.distTo(app.sketchDrawCurrent);
            std::vector<Vec3> pts;
            for (int j = 0; j <= 64; j++) {
                float a = (float)j / 64 * 2.0f * PI;
                Vec2 p = app.sketchDrawStart + Vec2{std::cos(a)*r, std::sin(a)*r};
                pts.push_back(app.sketch.sketchToWorld(p));
            }
            for (size_t j = 0; j + 1 < pts.size(); j++)
                app.renderer.drawLine3D(pts[j], pts[j+1], app.camera, {1,1,0,0.8f}, 1.5f);
            break;
        }
        case Tool::SketchRect: {
            Vec2 a = app.sketchDrawStart;
            Vec2 b = app.sketchDrawCurrent;
            if (app.dimInput.lengthSet && app.dimInput.angleSet)
                b = a + Vec2{app.dimInput.length, app.dimInput.angle};
            Vec3 c0 = app.sketch.sketchToWorld(a);
            Vec3 c1 = app.sketch.sketchToWorld({b.x, a.y});
            Vec3 c2 = app.sketch.sketchToWorld(b);
            Vec3 c3 = app.sketch.sketchToWorld({a.x, b.y});
            app.renderer.drawLine3D(c0, c1, app.camera, {1,1,0,0.8f}, 1.5f);
            app.renderer.drawLine3D(c1, c2, app.camera, {1,1,0,0.8f}, 1.5f);
            app.renderer.drawLine3D(c2, c3, app.camera, {1,1,0,0.8f}, 1.5f);
            app.renderer.drawLine3D(c3, c0, app.camera, {1,1,0,0.8f}, 1.5f);
            break;
        }
        default: break;
    }
    glEnable(GL_DEPTH_TEST);

    // Live measurement labels near cursor
    {
        Mat4 vp = app.camera.projMatrix() * app.camera.viewMatrix();
        Vec4 clip = vp * Vec4(curW, 1.0f);
        if (clip.w > 0) {
            float sx = (clip.x / clip.w * 0.5f + 0.5f) * app.windowWidth;
            float sy = (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * app.windowHeight;
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            char buf[64];

            if (app.currentTool == Tool::SketchLine) {
                Vec2 delta = app.sketchDrawCurrent - app.sketchDrawStart;
                float len = delta.length();
                float ang = std::atan2(delta.y, delta.x) * RAD2DEG;
                std::snprintf(buf, sizeof(buf), "%.2f mm  %.1f\xC2\xB0", len, ang);
            } else if (app.currentTool == Tool::SketchCircle) {
                float r = app.dimInput.lengthSet ? app.dimInput.length
                            : app.sketchDrawStart.distTo(app.sketchDrawCurrent);
                std::snprintf(buf, sizeof(buf), "R %.2f mm", r);
            } else if (app.currentTool == Tool::SketchRect) {
                Vec2 a = app.sketchDrawStart;
                Vec2 b = app.sketchDrawCurrent;
                if (app.dimInput.lengthSet && app.dimInput.angleSet)
                    b = a + Vec2{app.dimInput.length, app.dimInput.angle};
                float w = std::abs(b.x - a.x);
                float h = std::abs(b.y - a.y);
                std::snprintf(buf, sizeof(buf), "%.2f x %.2f mm", w, h);
            } else {
                buf[0] = '\0';
            }

            if (buf[0]) {
                ImVec2 textSize = ImGui::CalcTextSize(buf);
                float px = sx + 16;
                float py = sy - textSize.y - 8;
                dl->AddRectFilled(ImVec2(px - 4, py - 2),
                    ImVec2(px + textSize.x + 4, py + textSize.y + 2),
                    IM_COL32(30, 30, 35, 220), 4.0f);
                dl->AddText(ImVec2(px, py), IM_COL32(255, 220, 100, 255), buf);
            }
        }
    }
}

// ---- Draw snap overlays (indicator, label, constraint, inference) ----
static void drawSnapOverlays() {
    if (!(app.inSketchMode && app.currentSnap.valid())) return;

    Vec3 snapWorld = app.sketch.sketchToWorld(app.currentSnap.point);
    app.renderer.drawSnapIndicator(snapWorld, app.camera, app.currentSnap.type);

    // Snap type label
    {
        Mat4 vp = app.camera.projMatrix() * app.camera.viewMatrix();
        Vec4 clip = vp * Vec4(snapWorld, 1.0f);
        if (clip.w > 0) {
            float sx = (clip.x / clip.w * 0.5f + 0.5f) * app.windowWidth;
            float sy = (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * app.windowHeight;
            const char* label = nullptr;
            ImU32 col = IM_COL32(100, 220, 100, 230);
            switch (app.currentSnap.type) {
                case SnapType::Endpoint:      label = "Endpoint"; break;
                case SnapType::Midpoint:       label = "Midpoint"; col = IM_COL32(100, 200, 255, 230); break;
                case SnapType::Center:         label = "Center"; col = IM_COL32(255, 180, 100, 230); break;
                case SnapType::Origin:         label = "Origin"; col = IM_COL32(255, 100, 100, 230); break;
                case SnapType::Grid:           label = "Grid"; col = IM_COL32(180, 180, 180, 200); break;
                case SnapType::OnLine:         label = "On Line"; col = IM_COL32(180, 220, 100, 230); break;
                case SnapType::Horizontal:     label = "Horizontal"; col = IM_COL32(100, 255, 100, 230); break;
                case SnapType::Vertical:       label = "Vertical"; col = IM_COL32(100, 255, 100, 230); break;
                case SnapType::Parallel:       label = "Parallel"; col = IM_COL32(200, 160, 255, 230); break;
                case SnapType::Perpendicular:  label = "Perpendicular"; col = IM_COL32(200, 160, 255, 230); break;
                case SnapType::Intersection:   label = "Intersection"; col = IM_COL32(255, 255, 100, 230); break;
                default: break;
            }
            if (label) {
                ImDrawList* dl = ImGui::GetForegroundDrawList();
                ImVec2 textSize = ImGui::CalcTextSize(label);
                float px = sx - textSize.x * 0.5f;
                float py = sy + 12;
                dl->AddRectFilled(ImVec2(px - 3, py - 1),
                    ImVec2(px + textSize.x + 3, py + textSize.y + 1),
                    IM_COL32(20, 20, 25, 200), 3.0f);
                dl->AddText(ImVec2(px, py), col, label);
            }
        }
    }

    // Constraint symbol
    if (app.currentSnap.type == SnapType::Horizontal ||
        app.currentSnap.type == SnapType::Vertical ||
        app.currentSnap.type == SnapType::Parallel ||
        app.currentSnap.type == SnapType::Perpendicular) {
        Vec3 refDir = app.sketch.sketchToWorld(app.currentSnap.refLineDir) - app.sketch.sketchToWorld({0,0});
        app.renderer.drawConstraintSymbol(snapWorld, app.camera, app.currentSnap.type, refDir);
    }

    // Inference line
    if (app.sketchDrawing && (app.currentSnap.type == SnapType::Horizontal ||
        app.currentSnap.type == SnapType::Vertical)) {
        Vec3 startW = app.sketch.sketchToWorld(app.sketchDrawStart);
        Color dashCol = {0.3f, 0.8f, 0.3f, 0.4f};
        glDisable(GL_DEPTH_TEST);
        app.renderer.drawLine3D(startW, snapWorld, app.camera, dashCol, 1.0f);
        glEnable(GL_DEPTH_TEST);
    }
}

// ---- Draw measure tool overlay ----
static void drawMeasureOverlay() {
    if (!(app.currentTool == Tool::Measure && app.measureHasFirst)) return;

    Ray ray = app.camera.screenToRay(app.mouseX, app.mouseY);
    Plane ground({0,1,0}, Vec3{0,0,0});
    float t = ground.intersectRay(ray);
    Vec3 curPoint = t > 0 ? ray.at(t) : app.measurePointA;
    app.renderer.drawLine3D(app.measurePointA, curPoint, app.camera, {1, 0.5f, 0}, 2.0f);

    Vec3 mid = (app.measurePointA + curPoint) * 0.5f;
    Mat4 vp = app.camera.projMatrix() * app.camera.viewMatrix();
    Vec4 clip = vp * Vec4(mid, 1.0f);
    if (clip.w > 0) {
        float sx = (clip.x / clip.w * 0.5f + 0.5f) * app.windowWidth;
        float sy = (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * app.windowHeight;
        float d = measureDistance(app.measurePointA, curPoint);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f mm", d);
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 textSize = ImGui::CalcTextSize(buf);
        float px = sx - textSize.x * 0.5f;
        float py = sy - textSize.y - 6;
        dl->AddRectFilled(ImVec2(px - 4, py - 2),
            ImVec2(px + textSize.x + 4, py + textSize.y + 2),
            IM_COL32(30, 30, 35, 220), 4.0f);
        dl->AddText(ImVec2(px, py), IM_COL32(255, 180, 50, 255), buf);
    }
}

// ---- Full scene render ----
void renderScene() {
    int fbWidth = app.camera.viewportWidth;
    int fbHeight = app.camera.viewportHeight;

    app.renderer.beginFrame(fbWidth, fbHeight, 0.106f, 0.114f, 0.137f);

    // Slightly lighter at the top than the bottom — enough to give the model a
    // horizon to sit against without reading as a decorative backdrop.
    app.renderer.drawBackgroundGradient(
        Color{0.137f, 0.149f, 0.180f, 1.0f},
        Color{0.086f, 0.094f, 0.114f, 1.0f});

    app.renderer.drawGrid(app.camera, 1.0f, 50.0f);

    glDisable(GL_DEPTH_TEST);
    drawPlaneIndicators();
    glEnable(GL_DEPTH_TEST);

    if (app.planePick == PlanePickMode::WaitingForPlane ||
        app.currentTool == Tool::ConstructionPlane) {
        app.renderer.drawOriginCube(app.camera, 3.0f, app.hoveredOriginPlane);
    }

    // Solids
    for (int i = 0; i < (int)app.solids.size(); i++) {
        bool vis = (i < (int)app.solidVisible.size()) ? app.solidVisible[i] : true;
        if (!vis) continue;
        Color c = (i < (int)app.solidColors.size()) ? app.solidColors[i] : app.solids[i].color;
        if (app.selectedSolidSet.count(i)) c = {0.4f, 0.6f, 0.9f, 1.0f};
        else if (i == app.selectedSolidIdx) c = {0.4f, 0.6f, 0.9f, 1.0f};
        
        // Use preview solid if chamfer/fillet is being previewed on this solid
        const cad::Solid& solidToRender = (app.edgeModifierPreviewing && i == app.edgeModifierTargetSolidIdx) 
            ? app.edgeModifierPreviewSolid 
            : app.solids[i];
        
        app.renderer.drawSolid(solidToRender, app.camera, c);
        if (app.showSolidEdges) {
            // Slightly blue-black rather than pure black — pure black edges on
            // a cool grey body read as a printing artefact.
            app.renderer.drawEdges(solidToRender, app.camera, {0.09f, 0.10f, 0.13f, 0.85f}, 1.4f);
        }
    }

    // Extrude preview
    if (app.extrudePreviewing && !app.extrudePreviewSolids.empty()) {
        Color previewCol = (app.extrudeBoolOp == ExtrudeBoolOp::Cut)
            ? Color{1.0f, 0.3f, 0.3f, 0.35f}
            : Color{0.3f, 0.7f, 1.0f, 0.35f};
        for (auto& ps : app.extrudePreviewSolids) {
            app.renderer.drawExtrudePreview(ps, app.camera, previewCol);
        }
    }

    drawExtrudeArrow();
    drawMoveGizmo();
    drawPlaneHandle();
    drawEdgeModifierGizmo();
    drawProfileHighlights();
    drawMarqueeRect();

    // ---- Construction planes ----
    // Drawn unconditionally.  This used to sit inside the face-hover branch, so
    // a plane only appeared while the cursor happened to be over a solid's
    // face — it blinked out whenever you orbited the pointer onto empty space,
    // which looked like the plane vanishing at certain angles.
    for (int i = 0; i < (int)app.constructionPlanes.size(); i++) {
        const auto& cp = app.constructionPlanes[i];
        if (!cp.visible) continue;

        // Two tangent axes spanning the plane, for the quad we draw.
        Vec3 n = cp.plane.normal;
        Vec3 up = (std::abs(n.y) < 0.9f) ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        Vec3 t1 = n.cross(up).normalized();
        Vec3 t2 = n.cross(t1).normalized();
        float e = cp.extent;
        Vec3 corners[4] = {
            cp.origin + t1 * e - t2 * e,
            cp.origin + t1 * e + t2 * e,
            cp.origin - t1 * e + t2 * e,
            cp.origin - t1 * e - t2 * e,
        };
        std::vector<Vec3> quad = {corners[0], corners[1], corners[2], corners[3]};

        bool sel = (i == app.selectedConstructionPlaneIdx);
        bool hov = (i == app.hoveredConstructionPlaneIdx);
        Color fill = sel ? Color{0.40f, 0.70f, 1.0f, 0.20f}
                   : hov ? Color{0.60f, 0.90f, 1.0f, 0.16f}
                         : Color{0.50f, 0.80f, 1.0f, 0.09f};
        Color edge = sel ? Color{0.40f, 0.70f, 1.0f, 0.95f}
                   : hov ? Color{0.70f, 0.95f, 1.0f, 0.85f}
                         : Color{0.50f, 0.80f, 1.0f, 0.50f};

        glDisable(GL_DEPTH_TEST);
        app.renderer.drawProfileFill(quad, app.camera, fill);
        for (int j = 0; j < 4; j++) {
            app.renderer.drawLine3D(corners[j], corners[(j + 1) % 4], app.camera, edge, 1.5f);
        }
        glEnable(GL_DEPTH_TEST);
    }

    // Face/edge hover highlights
    if (app.hoveredFaceIdx >= 0 && app.hoveredSolidIdx >= 0 &&
        app.hoveredSolidIdx < (int)app.solids.size() &&
        app.hoveredFaceIdx < (int)app.solids[app.hoveredSolidIdx].faces.size()) {
        if (app.currentTool == Tool::Extrude ||
            app.planePick == PlanePickMode::WaitingForPlane ||
            app.currentTool == Tool::ConstructionPlane) {
            // Highlight the entire coplanar region, not the single fragment
            // the ray happened to land on.
            const Solid& hs = app.solids[app.hoveredSolidIdx];
            if (app.hoveredRegionSolid != app.hoveredSolidIdx ||
                app.hoveredRegionFace != app.hoveredFaceIdx) {
                app.hoveredFaceRegion = hs.coplanarRegion((uint32_t)app.hoveredFaceIdx);
                app.hoveredRegionSolid = app.hoveredSolidIdx;
                app.hoveredRegionFace = app.hoveredFaceIdx;
            }
            for (uint32_t fi : app.hoveredFaceRegion) {
                if (fi < hs.faces.size()) {
                    app.renderer.drawFaceHighlight(hs.faces[fi], app.camera,
                                                   {0.4f, 0.8f, 1.0f, 0.3f});
                }
            }
        }
    }
    if (app.hoveredEdgeIdx >= 0 && app.hoveredEdgeSolidIdx >= 0 &&
        app.hoveredEdgeSolidIdx < (int)app.solids.size() &&
        app.hoveredEdgeIdx < (int)app.solids[app.hoveredEdgeSolidIdx].edges.size() &&
        (app.currentTool == Tool::Chamfer || app.currentTool == Tool::Fillet ||
         app.currentTool == Tool::Select)) {
        const Solid& es = app.solids[app.hoveredEdgeSolidIdx];

        // Preview exactly what a click would select: the whole logical curve,
        // so hovering a bore rim lights the entire ring rather than one facet.
        if (app.hoveredEdgeGroupSolid != app.hoveredEdgeSolidIdx ||
            app.hoveredEdgeGroupEdge != app.hoveredEdgeIdx) {
            app.hoveredEdgeGroup = es.curveGroupEdges((uint32_t)app.hoveredEdgeIdx);
            app.hoveredEdgeGroupSolid = app.hoveredEdgeSolidIdx;
            app.hoveredEdgeGroupEdge = app.hoveredEdgeIdx;
        }
        Color hi = (app.currentTool == Tool::Select) ? Color{0.45f, 0.85f, 1.0f, 1.0f}
                                                     : Color{1.0f, 0.95f, 0.25f, 1.0f};
        for (uint32_t gi : app.hoveredEdgeGroup) {
            if (gi >= es.edges.size()) continue;
            app.renderer.drawEdgeHighlight(es.edges[gi].start, es.edges[gi].end,
                                           app.camera, hi, 3.0f);
        }
    }
    if ((app.currentTool == Tool::Chamfer || app.currentTool == Tool::Fillet) &&
        app.edgeModifierTargetSolidIdx >= 0 && app.edgeModifierTargetSolidIdx < (int)app.solids.size()) {
        const auto& solid = app.solids[app.edgeModifierTargetSolidIdx];
        for (int ei : app.selectedEdgeSet) {
            if (ei < 0 || ei >= (int)solid.edges.size()) continue;
            const auto& edge = solid.edges[ei];
            app.renderer.drawEdgeHighlight(edge.start, edge.end, app.camera, {1.0f, 0.55f, 0.0f}, 4.0f);
        }
    }
    if ((app.currentTool == Tool::Chamfer || app.currentTool == Tool::Fillet) && app.hoveredCornerValid) {
        app.renderer.drawSnapIndicator(app.hoveredCornerPos, app.camera, SnapType::Endpoint);
    }

    // Sketch
    bool showSketch = app.sketchVisible && !app.sketch.entities.empty();
    if (showSketch) {
        app.renderer.drawSketch(app.sketch, app.camera);
    }

    drawSketchPreview();
    drawSnapOverlays();
    drawMeasureOverlay();

    app.renderer.endFrame();
}
