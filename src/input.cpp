#include "input.h"
#include "tool_edge_modifier.h"
#include "extrude.h"
#include "tool_move.h"
#include "camera.h"
#include "picking.h"
#include "undo.h"
#include "save_load.h"
#include "geometry.h"
#include "csg.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

using namespace cad;


// ---- Helpers: keep solid metadata in sync ----
void addSolid(const Solid& solid, const std::string& name) {
    app.solids.push_back(solid);
    std::string n = name;
    if (n == "Body") n = "Body " + std::to_string(app.solids.size());
    app.solidNames.push_back(n);
    app.solidVisible.push_back(true);
    app.solidColors.push_back(solid.color);
}

void removeSolid(int index) {
    if (index < 0 || index >= (int)app.solids.size()) return;
    app.solids.erase(app.solids.begin() + index);
    if (index < (int)app.solidNames.size())
        app.solidNames.erase(app.solidNames.begin() + index);
    if (index < (int)app.solidVisible.size())
        app.solidVisible.erase(app.solidVisible.begin() + index);
    if (index < (int)app.solidColors.size())
        app.solidColors.erase(app.solidColors.begin() + index);

    // Adjust all global solid indices that shifted
    auto adjust = [&](int& idx) {
        if (idx == index) idx = -1;
        else if (idx > index) idx--;
    };
    adjust(app.selectedSolidIdx);
    adjust(app.extrudeTargetSolidIdx);
    adjust(app.extrudeFaceSolidIdx);
    adjust(app.sketchSourceSolidIdx);
    adjust(app.hoveredSolidIdx);
    app.selectedFaceIdx = -1;
    app.selectedEdgeIdx = -1;
    app.selectedSolidSet.clear();
    app.selectedFaceSet.clear();
    app.selectedEdgeSet.clear();
}

// ---- Collect magnetic reference geometry for the active sketch ----
// Any face lying in the sketch plane contributes its boundary, so you can snap
// to the corners, edges and centre of the face you are drawing on — and to any
// other geometry that happens to be coplanar with it.
void refreshSketchReferences() {
    std::vector<std::vector<Vec3>> loops;
    const Plane& sp = app.sketch.sketchPlane;

    for (size_t si = 0; si < app.solids.size(); si++) {
        if (si < app.solidVisible.size() && !app.solidVisible[si]) continue;
        for (const auto& face : app.solids[si].faces) {
            if (face.outerLoop.size() < 3) continue;
            // Coplanar means parallel normals and every vertex on the plane.
            if (std::abs(face.normal.dot(sp.normal)) < 0.999f) continue;
            bool onPlane = true;
            for (const Vec3& p : face.outerLoop) {
                if (std::abs(sp.distTo(p)) > 1e-3f) { onPlane = false; break; }
            }
            if (onPlane) loops.push_back(face.outerLoop);
        }
    }

    app.sketch.setReferenceLoops(loops);
}

// ---- Enter sketch mode on a plane ----
void enterSketchOnPlane(Plane plane, Vec3 origin, int sourceSolidIdx, int sourceFaceIdx, int sourcePlaneIdx) {
    app.sketch.setup(plane, origin);
    app.sketch.active = true;
    app.inSketchMode = true;
    app.currentTool = app.pendingTool;
    app.planePick = PlanePickMode::None;
    app.sketchDrawing = false;
    app.dimInput.reset();

    // Track the source face for face-splitting extrude
    app.sketchSourceSolidIdx = sourceSolidIdx;
    app.sketchSourceFaceIdx = sourceFaceIdx;
    app.sketchSourcePlaneIdx = sourcePlaneIdx;
    if (sourceSolidIdx >= 0 && sourceFaceIdx >= 0 &&
        sourceSolidIdx < (int)app.solids.size() &&
        sourceFaceIdx < (int)app.solids[sourceSolidIdx].faces.size()) {
        app.sketchSourceFaceLoop = app.solids[sourceSolidIdx].faces[sourceFaceIdx].outerLoop;
    } else {
        app.sketchSourceFaceLoop.clear();
    }

    refreshSketchReferences();

    animateCameraToPlane(plane, origin);
    app.statusText = toolName(app.currentTool) + std::string(" — click to place points (mm)");
}

// ---- Start tool that needs a plane ----
void startSketchTool(Tool tool) {
    app.pendingTool = tool;

    if (app.inSketchMode) {
        app.currentTool = tool;
        app.sketchDrawing = false;
        app.dimInput.reset();
        app.statusText = toolName(tool) + std::string(" — click to place points (mm)");
        return;
    }

    if (app.selectedFaceIdx >= 0 && app.selectedSolidIdx >= 0 &&
        app.selectedSolidIdx < (int)app.solids.size() &&
        app.selectedFaceIdx < (int)app.solids[app.selectedSolidIdx].faces.size()) {
        const auto& face = app.solids[app.selectedSolidIdx].faces[app.selectedFaceIdx];
        if (!face.outerLoop.empty()) {
            enterSketchOnPlane(Plane(face.normal, face.outerLoop[0]), face.centroid(),
                               app.selectedSolidIdx, app.selectedFaceIdx);
            return;
        }
    }

    app.planePick = PlanePickMode::WaitingForPlane;
    app.statusText = "Select a plane: click XY / XZ / YZ or a face";
}

// ---- Merge the selected bodies into one ----
void performJoinSelection() {
    std::vector<int> picks;
    for (int i : app.selectedSolidSet)
        if (i >= 0 && i < (int)app.solids.size()) picks.push_back(i);
    if (picks.size() < 2) {
        app.statusText = "Join needs at least two bodies";
        return;
    }

    undoStack.pushState("Join");

    // Union everything into the lowest-indexed body, then drop the rest from
    // the highest index down so the earlier indices stay valid.
    std::sort(picks.begin(), picks.end());
    Solid merged = app.solids[picks[0]];
    for (size_t i = 1; i < picks.size(); i++)
        merged = csgUnion(merged, app.solids[picks[i]]);

    app.solids[picks[0]] = merged;
    for (size_t i = picks.size(); i-- > 1; ) removeSolid(picks[i]);

    app.selectedSolidSet.clear();
    app.selectedSolidSet.insert(picks[0]);
    app.selectedSolidIdx = picks[0];
    app.statusText = "Joined " + std::to_string(picks.size()) + " bodies";
    app.currentTool = Tool::Select;
}

// ---- Frame the whole model ----
void fitViewToModel() {
    if (app.solids.empty()) {
        app.camera.target = {0, 0, 0};
        app.camera.distance = 30.0f;
        return;
    }
    AABB bb;
    for (const auto& s : app.solids) {
        AABB sb = s.bounds();
        bb.expand(sb.min);
        bb.expand(sb.max);
    }
    app.camera.fitTo(bb);
}

// ---- GLFW Callbacks ----
void scrollCallback(GLFWwindow* win, double xoff, double yoff) {
    if (ImGui::GetIO().WantCaptureMouse) return;

    bool shift = glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                 glfwGetKey(win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

    // Shift + two-finger scroll pans, matching the gesture most Mac apps use
    // for horizontal scrolling.  Trackpads also report horizontal scroll
    // directly, so honour that without needing the modifier at all.
    if (shift || std::abs(xoff) > std::abs(yoff)) {
        app.camera.pan((float)xoff * 12.0f, (float)-yoff * 12.0f);
        return;
    }

    app.camera.zoomAt((float)yoff, app.mouseX, app.mouseY);
}

void keyCallback(GLFWwindow* win, int key, int, int action, int mods) {
    // Allow tool-switch hotkeys to work even when ImGui has keyboard focus
    if (ImGui::GetIO().WantCaptureKeyboard) {
        if (action == GLFW_PRESS) {
            bool isLetter = (key >= GLFW_KEY_A && key <= GLFW_KEY_Z);
            bool isControl = (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_TAB ||
                              key == GLFW_KEY_HOME || key == GLFW_KEY_DELETE ||
                              key == GLFW_KEY_BACKSPACE);
            bool hasModifier = (mods & GLFW_MOD_SUPER) || (mods & GLFW_MOD_CONTROL) ||
                               (mods & GLFW_MOD_SHIFT);
            if (isLetter || isControl || hasModifier) {
                // pass through to our handler
            } else {
                return;
            }
        } else {
            return;
        }
    }

    bool cmd = (mods & GLFW_MOD_SUPER) || (mods & GLFW_MOD_CONTROL);
    bool shift = (mods & GLFW_MOD_SHIFT);

    if (action == GLFW_PRESS) {
        // Undo/Redo: Cmd+Z / Cmd+Shift+Z
        if (cmd && key == GLFW_KEY_Z) {
            if (shift) {
                if (undoStack.redo()) app.statusText = "Redo";
            } else {
                if (undoStack.undo()) app.statusText = "Undo";
            }
            return;
        }

        // Save: Cmd+S
        if (cmd && key == GLFW_KEY_S) {
            saveProjectInteractive();
            return;
        }

        // Open: Cmd+O
        if (cmd && key == GLFW_KEY_O) {
            loadProjectInteractive();
            return;
        }

        // Export STL: Cmd+Shift+E
        if (cmd && shift && key == GLFW_KEY_E) {
            app.showExportStlDialog = true;
            app.focusExportStlPath = true;
            return;
        }

        if (app.dimInput.active && key == GLFW_KEY_TAB) {
            app.dimInput.focusAngle = true;
            return;
        }

        // Shortcut overlay
        if (key == GLFW_KEY_TAB) {
            app.panelsVisible = !app.panelsVisible;
            app.statusText = app.panelsVisible ? "Panels shown" : "Panels hidden — Tab to restore";
            return;
        }
        if (key == GLFW_KEY_SLASH && shift) {
            app.showShortcutOverlay = !app.showShortcutOverlay;
            return;
        }

        switch (key) {
            case GLFW_KEY_ESCAPE:
                if (app.showShortcutOverlay) {
                    app.showShortcutOverlay = false;
                } else if (app.extrudeDragging) {
                    // Cancel active extrude drag
                    app.extrudeDragging = false;
                    app.extrudePreviewing = false;
                    app.extrudePreviewSolids.clear();
                    app.extrudeProfiles.clear();
                    app.extrudeFaceSolidIdx = -1;
                    app.extrudeFaceIdx = -1;
                    app.extrudeFaceProfile.clear();
                    app.extrudeFaceHoles.clear();
                    app.extrudeSelectedProfile = -1;
                    app.extrudeInputActive = false;
                    app.currentTool = Tool::Select;
                    app.statusText = "Extrude cancelled";
                } else if (app.extrudePreviewing) {
                    // Cancel extrude preview
                    app.extrudePreviewing = false;
                    app.extrudePreviewSolids.clear();
                    app.extrudeProfiles.clear();
                    app.extrudeSelectedProfile = -1;
                    app.extrudeInputActive = false;
                    app.currentTool = Tool::Select;
                    app.statusText = "Extrude cancelled";
                } else if (app.planePick != PlanePickMode::None) {
                    app.planePick = PlanePickMode::None;
                    app.currentTool = Tool::Select;
                    app.statusText = "Cancelled";
                } else if (app.sketchDrawing) {
                    // End the current line chain but stay in sketch mode
                    app.sketchDrawing = false;
                    app.dimInput.reset();
                    app.statusText = "Line chain ended — click to start a new line";
                } else if (app.inSketchMode && app.currentTool != Tool::Select) {
                    // First Esc in sketch: drop back to Select (cancel active tool)
                    app.currentTool = Tool::Select;
                    app.sketchDrawing = false;
                    app.dimInput.reset();
                    app.statusText = "Tool cancelled";
                } else if (app.inSketchMode) {
                    // Second Esc: exit sketch mode
                    app.inSketchMode = false;
                    app.sketchDrawing = false;
                    app.dimInput.reset();
                    app.sketchSourceSolidIdx = -1;
                    app.sketchSourceFaceIdx = -1;
                    app.sketchSourceFaceLoop.clear();
                    app.currentTool = Tool::Select;
                    app.statusText = "Finished sketch";
                } else {
                    app.currentTool = Tool::Select;
                    app.selectedSolidIdx = -1;
                    app.selectedFaceIdx = -1;
                    app.selectedEdgeIdx = -1;
                    app.selectedSolidSet.clear();
                    app.selectedFaceSet.clear();
                    app.selectedEdgeSet.clear();
                    app.statusText = "Ready";
                }
                // Always clean up transient tool states on ESC regardless of branch
                clearActiveEdgeModifierSelection();
                app.extrudeDragging = false;
                app.edgeModifierDragging = false;
                app.planePick = PlanePickMode::None;
                break;
            case GLFW_KEY_DELETE:
            case GLFW_KEY_BACKSPACE:
                if (app.dimInput.active) return;
                if (app.inSketchMode) {
                    undoStack.pushState("Delete sketch entities");
                    app.sketch.deleteSelected();
                } else if (app.selectedSolidIdx >= 0 && app.selectedSolidIdx < (int)app.solids.size()) {
                    undoStack.pushState("Delete solid");
                    removeSolid(app.selectedSolidIdx);
                    app.statusText = "Deleted solid";
                }
                break;
            case GLFW_KEY_HOME:
                fitViewToModel();
                break;
            case GLFW_KEY_L:
                startSketchTool(Tool::SketchLine);
                break;
            case GLFW_KEY_C:
                if (shift) {
                    app.currentTool = Tool::Chamfer;
                    clearActiveEdgeModifierSelection();
                    app.statusText = "Chamfer — select edges/corners, drag or set mm, Enter to apply";
                } else {
                    startSketchTool(Tool::SketchCircle);
                }
                break;
            case GLFW_KEY_R:
                startSketchTool(Tool::SketchRect);
                break;
            case GLFW_KEY_T:
                if (app.inSketchMode) {
                    app.currentTool = Tool::SketchTrim;
                    app.sketchDrawing = false;
                    app.dimInput.reset();
                    app.statusText = "Trim — click on a line segment to trim";
                }
                break;
            case GLFW_KEY_O:
                if (app.inSketchMode && !cmd) {
                    app.currentTool = Tool::SketchOffset;
                    app.sketchDrawing = false;
                    app.dimInput.reset();
                    app.statusText = "Offset — profiles will be offset";
                }
                break;
            case GLFW_KEY_E:
                enterExtrudeMode();
                break;
            case GLFW_KEY_F:
                if (shift) {
                    app.currentTool = Tool::Fillet;
                    clearActiveEdgeModifierSelection();
                    app.statusText = "Fillet — select edges/corners, drag or set radius, Enter to apply";
                } else {
                    // Plain F fits the view.  A MacBook keyboard has no Home
                    // key, so Home alone left this unreachable on a laptop.
                    fitViewToModel();
                    app.statusText = "View fit to model";
                }
                break;
            case GLFW_KEY_G:
                app.currentTool = Tool::Move;
                updateMoveGizmo();
                app.statusText = "Move — select a body, then drag an axis arrow";
                break;
            case GLFW_KEY_M:
                app.currentTool = Tool::Measure;
                app.measureHasFirst = false;
                app.statusText = "Measure — click two points (mm)";
                break;
            case GLFW_KEY_S:
                if (shift && !cmd) {
                    app.currentTool = Tool::Split;
                    app.statusText = "Split — select a solid, then a plane";
                }
                break;
            case GLFW_KEY_ENTER:
            case GLFW_KEY_KP_ENTER:
                // Finish Sketch or confirm extrude
                if (app.currentTool == Tool::Join) {
                    performJoinSelection();
                } else if (app.currentTool == Tool::Chamfer || app.currentTool == Tool::Fillet) {
                    applyActiveEdgeModifier();
                } else if (app.extrudePreviewing) {
                    if (std::abs(app.extrudeDistance) < 0.01f) {
                        app.statusText = "Set a distance before confirming";
                    } else {
                        performExtrude();
                    }
                } else if (app.inSketchMode && !app.sketchDrawing) {
                    app.inSketchMode = false;
                    app.sketchDrawing = false;
                    app.dimInput.reset();
                    app.sketchSourceSolidIdx = -1;
                    app.sketchSourceFaceIdx = -1;
                    app.sketchSourceFaceLoop.clear();
                    app.currentTool = Tool::Select;
                    app.statusText = "Finished sketch";
                }
                break;
        }
    }
}

// ---- Utility ----
Vec2 computeEndpoint(Vec2 start, float lengthMm, float angleDeg) {
    float rad = angleDeg * DEG2RAD;
    return start + Vec2{std::cos(rad) * lengthMm, std::sin(rad) * lengthMm};
}

// ---- Sketch click with dimension support ----
void handleSketchClick(Vec2 sketchPos) {
    SnapResult snap = app.sketchDrawing
        ? app.sketch.findSnap(sketchPos, true, app.sketchDrawStart)
        : app.sketch.findSnap(sketchPos);
    Vec2 pos = snap.valid() ? snap.point : sketchPos;

    if (!app.sketchDrawing) {
        app.sketchDrawing = true;
        app.sketchDrawStart = pos;
        app.sketchDrawCurrent = pos;
        app.dimInput.activate();
    } else {
        Vec2 finalPos = pos;

        if (app.dimInput.lengthSet) {
            float len = app.dimInput.length;
            float ang = app.dimInput.angle;
            if (!app.dimInput.angleSet) {
                Vec2 delta = pos - app.sketchDrawStart;
                ang = std::atan2(delta.y, delta.x) * RAD2DEG;
            }
            finalPos = computeEndpoint(app.sketchDrawStart, len, ang);
        }

        undoStack.pushState("Add sketch entity");

        switch (app.currentTool) {
            case Tool::SketchLine:
                app.sketch.addLine(app.sketchDrawStart, finalPos);
                app.sketchDrawStart = finalPos;
                app.dimInput.activate();
                break;
            case Tool::SketchCircle: {
                float r = app.dimInput.lengthSet ? app.dimInput.length : app.sketchDrawStart.distTo(finalPos);
                if (r > EPSILON) app.sketch.addCircle(app.sketchDrawStart, r);
                app.sketchDrawing = false;
                app.dimInput.reset();
                break;
            }
            case Tool::SketchRect: {
                if (app.dimInput.lengthSet && app.dimInput.angleSet) {
                    float w = app.dimInput.length;
                    float h = app.dimInput.angle;
                    finalPos = app.sketchDrawStart + Vec2{w, h};
                }
                app.sketch.addRectangle(app.sketchDrawStart, finalPos);
                app.sketchDrawing = false;
                app.dimInput.reset();
                break;
            }
            default:
                app.sketchDrawing = false;
                app.dimInput.reset();
                break;
        }
    }
}

// ---- Ray-quad intersection for origin cube faces ----
static bool rayQuadIntersect(Ray ray, Vec3 v0, Vec3 v1, Vec3 v2, Vec3 v3, float& outT) {
    // Triangle 1: v0, v1, v2
    auto rayTri = [&](Vec3 a, Vec3 b, Vec3 c, float& t) -> bool {
        Vec3 e1 = b - a, e2 = c - a;
        Vec3 h = ray.dir.cross(e2);
        float det = e1.dot(h);
        if (std::abs(det) < EPSILON) return false;
        float invDet = 1.0f / det;
        Vec3 s = ray.origin - a;
        float u = s.dot(h) * invDet;
        if (u < 0 || u > 1) return false;
        Vec3 q = s.cross(e1);
        float v = ray.dir.dot(q) * invDet;
        if (v < 0 || u + v > 1) return false;
        t = e2.dot(q) * invDet;
        return t > 0;
    };

    float t1 = 1e18f, t2 = 1e18f;
    bool h1 = rayTri(v0, v1, v2, t1);
    bool h2 = rayTri(v0, v2, v3, t2);
    if (h1 || h2) {
        outT = std::min(t1, t2);
        return true;
    }
    return false;
}

void updateOriginCubeHover() {
    app.hoveredOriginPlane = -1;
    if (app.planePick != PlanePickMode::WaitingForPlane &&
        app.currentTool != Tool::ConstructionPlane) return;

    Ray ray = app.camera.screenToRay(app.mouseX, app.mouseY);
    float s = 3.0f;

    // Face 0: Top (XZ, Y=0)
    Vec3 f0[4] = {{0,0,0}, {s,0,0}, {s,0,s}, {0,0,s}};
    // Face 1: Front (XY, Z=0)
    Vec3 f1[4] = {{0,0,0}, {s,0,0}, {s,s,0}, {0,s,0}};
    // Face 2: Side (YZ, X=0)
    Vec3 f2[4] = {{0,0,0}, {0,0,s}, {0,s,s}, {0,s,0}};

    Vec3 (*faces[3])[4] = {&f0, &f1, &f2};

    float bestT = 1e18f;
    for (int i = 0; i < 3; i++) {
        float t;
        Vec3 (&f)[4] = *faces[i];
        if (rayQuadIntersect(ray, f[0], f[1], f[2], f[3], t)) {
            if (t < bestT) {
                bestT = t;
                app.hoveredOriginPlane = i;
            }
        }
    }
}

// ---- Right-click context menu handler ----
void handleRightClick() {
    // Right-click opens ImGui context menu — handled in UI
}

// ---- Mouse click dispatch ----
void handleMouseClick() {
    // Plane picking mode (for sketch tools only — Split has its own handler below)
    if (app.planePick == PlanePickMode::WaitingForPlane &&
        app.currentTool != Tool::Split) {
        if (app.hoveredOriginPlane >= 0) {
            Plane planes[3] = {
                Plane({0, 1, 0}, Vec3{0,0,0}),
                Plane({0, 0, 1}, Vec3{0,0,0}),
                Plane({1, 0, 0}, Vec3{0,0,0}),
            };
            enterSketchOnPlane(planes[app.hoveredOriginPlane], {0,0,0});
            app.hoveredOriginPlane = -1;
            return;
        }
        if (app.hoveredFaceIdx >= 0 && app.hoveredSolidIdx >= 0 &&
            app.hoveredSolidIdx < (int)app.solids.size()) {
            const auto& face = app.solids[app.hoveredSolidIdx].faces[app.hoveredFaceIdx];
            if (!face.outerLoop.empty()) {
                enterSketchOnPlane(Plane(face.normal, face.outerLoop[0]), face.centroid(),
                                   app.hoveredSolidIdx, app.hoveredFaceIdx);
            }
            return;
        }
        return;
    }

    // Sketch mode tools (but not when Extrude is active — let Extrude handle clicks)
    if (app.inSketchMode && app.currentTool != Tool::Extrude) {
        if (app.currentTool == Tool::SketchLine ||
            app.currentTool == Tool::SketchCircle ||
            app.currentTool == Tool::SketchRect) {
            Ray ray = app.camera.screenToRay(app.mouseX, app.mouseY);
            float t = app.sketch.sketchPlane.intersectRay(ray);
            if (t > 0) {
                Vec3 worldHit = ray.at(t);
                Vec2 sketchPos = app.sketch.worldToSketch(worldHit);
                handleSketchClick(sketchPos);
            }
            return;
        }

        // Trim tool
        if (app.currentTool == Tool::SketchTrim) {
            Ray ray = app.camera.screenToRay(app.mouseX, app.mouseY);
            float t = app.sketch.sketchPlane.intersectRay(ray);
            if (t > 0) {
                Vec3 worldHit = ray.at(t);
                Vec2 sketchPos = app.sketch.worldToSketch(worldHit);
                undoStack.pushState("Trim");
                if (app.sketch.trimAtPoint(sketchPos, app.sketch.snapDistance * 3.0f)) {
                    app.statusText = "Trimmed";
                } else {
                    app.statusText = "Nothing to trim";
                }
            }
            return;
        }

        // Offset tool
        if (app.currentTool == Tool::SketchOffset) {
            undoStack.pushState("Offset profiles");
            app.sketch.offsetProfiles(app.offsetDistance);
            app.statusText = "Offset applied";
            app.currentTool = Tool::Select;
            return;
        }

        // Select sketch entity
        if (app.currentTool == Tool::Select) {
            Ray ray = app.camera.screenToRay(app.mouseX, app.mouseY);
            float t = app.sketch.sketchPlane.intersectRay(ray);
            if (t > 0) {
                Vec3 worldHit = ray.at(t);
                Vec2 sketchPos = app.sketch.worldToSketch(worldHit);
                int idx = app.sketch.pickEntity(sketchPos, app.sketch.snapDistance * 2.0f);
                if (idx >= 0) {
                    // Clear selection first unless shift is held
                    if (!app.shiftDown) {
                        for (auto& e : app.sketch.entities) e.selected = false;
                    }
                    app.sketch.entities[idx].selected = !app.sketch.entities[idx].selected;
                    app.statusText = "Selected sketch entity";
                } else {
                    // Click empty space: deselect all
                    for (auto& e : app.sketch.entities) e.selected = false;
                    app.statusText = "Deselected";
                }
            }
            return;
        }
    }

    // Extrude tool
    if (app.currentTool == Tool::Extrude) {
        // If already dragging, confirm on click
        if (app.extrudeDragging) {
            handleExtrudeRelease();
            return;
        }

        // If previewing, clicking another face sets the extent to that face plane.
        if (app.extrudePreviewing && app.hoveredFaceIdx >= 0 && app.hoveredSolidIdx >= 0) {
            if (setExtrudeDistanceFromFace(app.hoveredSolidIdx, app.hoveredFaceIdx)) {
                return;
            }
        }

        // If a profile region is already selected and previewing, start drag
        if (app.extrudePreviewing && app.extrudeSelectedProfile != -1 &&
            (!app.extrudeProfiles.empty() || app.extrudeSelectedProfile == -2)) {
            app.extrudeDragging = true;
            app.extrudeDragStartX = app.mouseX;
            app.extrudeDragStartY = app.mouseY;
            app.extrudeDragStartDist = app.extrudeDistance;
            app.statusText = "Drag to set distance, release to confirm";
            return;
        }

        // Profile region picking: sketch on a source face, user hasn't picked yet
        if (app.inSketchMode && !app.sketchSourceFaceLoop.empty() &&
            !app.sketch.entities.empty() && app.extrudeSelectedProfile == -1) {
            // Cast ray to sketch plane to determine click position in 2D
            Ray ray = app.camera.screenToRay(app.mouseX, app.mouseY);
            float t = app.sketch.sketchPlane.intersectRay(ray);
            if (t > 0) {
                Vec3 worldHit = ray.at(t);
                Vec2 clickPos = app.sketch.worldToSketch(worldHit);

                // Get all sketch profiles in 2D
                auto profiles2D = app.sketch.extractProfiles2D();
                auto profiles3D = app.sketch.extractProfiles();

                // Convert source face loop to 2D
                std::vector<Vec2> faceBoundary2D;
                for (auto& pt : app.sketchSourceFaceLoop)
                    faceBoundary2D.push_back(app.sketch.worldToSketch(pt));

                // Check if click is inside any sketch profile
                int hitProfile = -1;
                for (int i = 0; i < (int)profiles2D.size(); i++) {
                    if (pointInPolygon2D(clickPos, profiles2D[i])) {
                        hitProfile = i;
                        break;
                    }
                }

                if (hitProfile >= 0) {
                    // Clicked inside a sketch profile → extrude just that profile
                    app.extrudeSelectedProfile = hitProfile;
                    app.extrudeProfiles.clear();
                    app.extrudeProfiles.push_back(profiles3D[hitProfile]);
                    app.extrudePreviewing = true;
                    app.extrudeArrowDir = app.sketch.sketchPlane.normal;

                    Vec3 center{0,0,0};
                    for (auto& p : profiles3D[hitProfile]) center += p;
                    if (!profiles3D[hitProfile].empty())
                        center = center * (1.0f / profiles3D[hitProfile].size());
                    app.extrudeArrowBase = center;

                    updateExtrudePreview();
                    app.extrudeInputActive = true;
                    app.extrudeInputFocus = true;
                    app.extrudeInputBuf[0] = '\0';
                    app.statusText = "Profile selected — type distance or drag arrow, Enter to confirm";
                    return;
                }

                // Check if click is inside the face boundary but outside all profiles
                bool inFace = pointInPolygon2D(clickPos, faceBoundary2D);
                if (inFace) {
                    // Clicked the complement region → extrude face with holes
                    app.extrudeSelectedProfile = -2; // special: complement
                    app.extrudeProfiles.clear(); // profiles used from sketch in performExtrude
                    app.extrudePreviewing = true;
                    app.extrudeArrowDir = app.sketch.sketchPlane.normal;

                    // Arrow at face centroid
                    Vec3 center{0,0,0};
                    for (auto& p : app.sketchSourceFaceLoop) center += p;
                    center = center * (1.0f / app.sketchSourceFaceLoop.size());
                    app.extrudeArrowBase = center;

                    updateExtrudePreview();
                    app.extrudeInputActive = true;
                    app.extrudeInputFocus = true;
                    app.extrudeInputBuf[0] = '\0';
                    app.statusText = "Face region selected — type distance or drag arrow, Enter to confirm";
                    return;
                }
            }
            // Click missed all regions
            app.statusText = "Click inside or outside the profile on the face";
            return;
        }

        // If already have profiles (no source face), start drag
        if (app.extrudePreviewing && !app.extrudeProfiles.empty()) {
            app.extrudeDragging = true;
            app.extrudeDragStartX = app.mouseX;
            app.extrudeDragStartY = app.mouseY;
            app.extrudeDragStartDist = app.extrudeDistance;
            app.statusText = "Drag to set distance, release to confirm";
            return;
        }

        // Click on a face → enter arrow drag mode with live preview
        if (app.hoveredFaceIdx >= 0 && app.hoveredSolidIdx >= 0 &&
            app.hoveredSolidIdx < (int)app.solids.size() &&
            app.hoveredFaceIdx < (int)app.solids[app.hoveredSolidIdx].faces.size()) {
            const Solid& hs = app.solids[app.hoveredSolidIdx];
            const auto& face = hs.faces[app.hoveredFaceIdx];
            app.extrudeFaceSolidIdx = app.hoveredSolidIdx;
            app.extrudeFaceIdx = app.hoveredFaceIdx;
            app.extrudeArrowDir = face.normal;

            // Push/pull acts on the whole coplanar region, not the single
            // fragment under the cursor — a boolean will have shattered a flat
            // face into many, and the bore through it becomes a hole loop.
            auto region = hs.coplanarRegion((uint32_t)app.hoveredFaceIdx);
            auto loops = hs.regionBoundaryLoops(region);

            app.extrudeFaceHoles.clear();
            if (!loops.empty()) {
                app.extrudeFaceProfile = loops[0];
                for (size_t li = 1; li < loops.size(); li++)
                    app.extrudeFaceHoles.push_back(loops[li]);
                Vec3 c{0, 0, 0};
                for (const Vec3& p : loops[0]) c += p;
                app.extrudeArrowBase = c * (1.0f / (float)loops[0].size());
            } else {
                app.extrudeFaceProfile = face.outerLoop;
                app.extrudeArrowBase = face.centroid();
            }

            app.extrudeDragging = true;
            app.extrudePreviewing = true;
            app.extrudeDragStartX = app.mouseX;
            app.extrudeDragStartY = app.mouseY;
            app.extrudeDragStartDist = app.extrudeDistance;
            app.extrudeProfiles.clear();
            app.extrudeProfiles.push_back(app.extrudeFaceProfile);
            updateExtrudePreview();
            app.statusText = "Drag to set distance, release to confirm";
            return;
        }
        // Click with sketch profiles → enter arrow drag mode
        if (!app.extrudeProfiles.empty()) {
            app.extrudeDragging = true;
            app.extrudeDragStartX = app.mouseX;
            app.extrudeDragStartY = app.mouseY;
            app.extrudeDragStartDist = app.extrudeDistance;
            updateExtrudePreview();
            app.statusText = "Drag to set distance, release to confirm";
            return;
        }
        return;
    }

    if (app.currentTool == Tool::Select || app.currentTool == Tool::Move) {
        // Multi-select with Shift
        if (app.shiftDown) {
            if (app.hoveredSolidIdx >= 0)
                app.selectedSolidSet.insert(app.hoveredSolidIdx);
        } else {
            app.selectedSolidSet.clear();
            app.selectedFaceSet.clear();
            app.selectedEdgeSet.clear();
        }
        app.selectedSolidIdx = app.hoveredSolidIdx;
        app.selectedFaceIdx = app.hoveredFaceIdx;
        app.selectedEdgeIdx = (app.hoveredEdgeSolidIdx == app.hoveredSolidIdx)
            ? app.hoveredEdgeIdx : -1;
        if (app.selectedSolidIdx >= 0) {
            app.selectedSolidSet.insert(app.selectedSolidIdx);
            std::string name = (app.selectedSolidIdx < (int)app.solidNames.size())
                ? app.solidNames[app.selectedSolidIdx]
                : ("Body " + std::to_string(app.selectedSolidIdx));
            app.statusText = "Selected " + name;

            // Select the whole coplanar region, so a face split into fragments
            // by a boolean still behaves as the one face the user clicked.
            if (app.selectedFaceIdx >= 0 &&
                app.selectedSolidIdx < (int)app.solids.size()) {
                const Solid& s = app.solids[app.selectedSolidIdx];
                auto region = s.coplanarRegion((uint32_t)app.selectedFaceIdx);
                for (uint32_t fi : region) app.selectedFaceSet.insert((int)fi);
                app.statusText += (region.size() > 1)
                    ? " — face (" + std::to_string(region.size()) + " fragments)"
                    : " — face";
            }
        }
    }

    if (app.currentTool == Tool::Chamfer || app.currentTool == Tool::Fillet) {
        if (isMouseInEdgeModifierChip()) {
            float value = (app.currentTool == Tool::Chamfer) ? app.chamferDistance : app.filletRadius;
            std::snprintf(app.edgeModifierInputBuf, sizeof(app.edgeModifierInputBuf), "%.3f", value);
            app.edgeModifierInputActive = true;
            app.edgeModifierInputFocus = true;
            app.statusText = "Type value and press Enter";
            return;
        }

        if (tryStartEdgeModifierDragFromGizmo()) {
            return;
        }

        if (tryStartEdgeModifierDragNearSelectedEdges()) {
            return;
        }

        auto addCornerEdges = [&](int solidIdx, Vec3 cornerPos) {
            if (solidIdx < 0 || solidIdx >= (int)app.solids.size()) return;
            const auto& solid = app.solids[solidIdx];
            for (int ei = 0; ei < (int)solid.edges.size(); ei++) {
                const auto& e = solid.edges[ei];
                if (e.start.distTo(cornerPos) < 0.001f || e.end.distTo(cornerPos) < 0.001f) {
                    app.selectedEdgeSet.insert(ei);
                }
            }
        };

        if (app.hoveredEdgeIdx >= 0 && app.hoveredEdgeSolidIdx >= 0 &&
            app.hoveredEdgeSolidIdx < (int)app.solids.size() &&
            app.hoveredEdgeIdx < (int)app.solids[app.hoveredEdgeSolidIdx].edges.size()) {
            if (app.edgeModifierTargetSolidIdx != app.hoveredEdgeSolidIdx) {
                clearActiveEdgeModifierSelection();
                app.edgeModifierTargetSolidIdx = app.hoveredEdgeSolidIdx;
            }

            // Expand to the full curved-edge group so a single click on any
            // segment of a fillet rim selects the whole logical curve.
            std::vector<uint32_t> groupEdges =
                app.solids[app.hoveredEdgeSolidIdx].curveGroupEdges((uint32_t)app.hoveredEdgeIdx);

            // Are all group edges already selected?  Used for shift-toggle.
            bool allSelected = true;
            for (uint32_t ei : groupEdges) {
                if (!app.selectedEdgeSet.count((int)ei)) { allSelected = false; break; }
            }

            if (!app.shiftDown && allSelected) {
                app.edgeModifierDragging = true;
                app.edgeModifierDragStartX = app.mouseX;
                app.edgeModifierDragStartY = app.mouseY;
                app.edgeModifierDragStartValue =
                    (app.currentTool == Tool::Chamfer) ? app.chamferDistance : app.filletRadius;
                app.statusText = "Drag to set amount, Enter to apply";
                return;
            }

            if (!app.shiftDown) app.selectedEdgeSet.clear();
            if (allSelected && app.shiftDown) {
                for (uint32_t ei : groupEdges) app.selectedEdgeSet.erase((int)ei);
            } else {
                for (uint32_t ei : groupEdges) app.selectedEdgeSet.insert((int)ei);
            }

            app.edgeModifierTargetSolidIdx = app.hoveredSolidIdx;
            app.selectedSolidIdx = app.hoveredSolidIdx;
            app.statusText = (groupEdges.size() > 1)
                ? "Curved edge selected (" + std::to_string(groupEdges.size()) +
                  " segments) — drag or set value, Enter to apply"
                : "Edge selected — drag or set value, Enter to apply";
            return;
        }

        if (app.hoveredCornerValid && app.hoveredCornerSolidIdx >= 0) {
            if (app.edgeModifierTargetSolidIdx != app.hoveredCornerSolidIdx) {
                clearActiveEdgeModifierSelection();
                app.edgeModifierTargetSolidIdx = app.hoveredCornerSolidIdx;
            }
            if (!app.shiftDown) app.selectedEdgeSet.clear();
            addCornerEdges(app.hoveredCornerSolidIdx, app.hoveredCornerPos);
            app.selectedSolidIdx = app.hoveredCornerSolidIdx;
            app.statusText = "Corner selected — connected edges added";
            return;
        }

        if (!app.shiftDown) {
            clearActiveEdgeModifierSelection();
            app.statusText = "Selection cleared";
        }
        return;
    }

    if (app.currentTool == Tool::Split && app.hoveredSolidIdx >= 0 &&
        app.hoveredSolidIdx < (int)app.solids.size()) {
        // First click: select solid to split
        if (app.planePick != PlanePickMode::WaitingForPlane) {
            app.selectedSolidIdx = app.hoveredSolidIdx;
            app.planePick = PlanePickMode::WaitingForPlane;
            std::string nm = (app.selectedSolidIdx < (int)app.solidNames.size())
                ? app.solidNames[app.selectedSolidIdx]
                : ("Body " + std::to_string(app.selectedSolidIdx));
            app.statusText = "Splitting " + nm + " — click a construction plane, an origin plane, or a face";
            return;
        }

        // Second interaction: cut with a construction plane clicked in the
        // viewport.  Checked before the origin cube so a plane sitting over the
        // cube still wins.
        if (app.planePick == PlanePickMode::WaitingForPlane &&
            app.hoveredConstructionPlaneIdx >= 0 &&
            app.selectedSolidIdx >= 0 && app.selectedSolidIdx < (int)app.solids.size()) {
            const auto& cp = app.constructionPlanes[app.hoveredConstructionPlaneIdx];
            undoStack.pushState("Split");
            auto [above, below] = split(app.solids[app.selectedSolidIdx], cp.plane);
            std::string srcName = (app.selectedSolidIdx < (int)app.solidNames.size())
                ? app.solidNames[app.selectedSolidIdx] : std::string("Body");
            app.solids[app.selectedSolidIdx] = above;
            addSolid(below, srcName + " (split)");
            app.planePick = PlanePickMode::None;
            app.currentTool = Tool::Select;
            app.statusText = "Split by " + cp.name + " — switch to Move to separate the halves";
            return;
        }

        // Second interaction: perform split via origin-plane cube
        if (app.planePick == PlanePickMode::WaitingForPlane && app.hoveredOriginPlane >= 0) {
            Plane planes[3] = {
                Plane({0, 1, 0}, Vec3{0,0,0}),
                Plane({0, 0, 1}, Vec3{0,0,0}),
                Plane({1, 0, 0}, Vec3{0,0,0}),
            };
            Plane plane = planes[app.hoveredOriginPlane];
            if (app.selectedSolidIdx >= 0 && app.selectedSolidIdx < (int)app.solids.size()) {
                auto [above, below] = split(app.solids[app.selectedSolidIdx], plane);
                undoStack.pushState("Split");
                app.solids[app.selectedSolidIdx] = above;
                addSolid(below, "Split " + (app.selectedSolidIdx < (int)app.solidNames.size()
                    ? app.solidNames[app.selectedSolidIdx] : std::string("Body")));
            }
            app.planePick = PlanePickMode::None;
            app.statusText = "Split performed";
            app.currentTool = Tool::Select;
            return;
        }

        // Second interaction: perform split via a face normal
        if (app.planePick == PlanePickMode::WaitingForPlane &&
            app.hoveredFaceIdx >= 0 &&
            app.selectedSolidIdx >= 0 && app.selectedSolidIdx < (int)app.solids.size()) {
            // The hovered solid for plane selection may differ from target
            int planeSolidIdx = app.hoveredSolidIdx;
            if (planeSolidIdx >= 0 && planeSolidIdx < (int)app.solids.size() &&
                app.hoveredFaceIdx < (int)app.solids[planeSolidIdx].faces.size()) {
                const auto& face = app.solids[planeSolidIdx].faces[app.hoveredFaceIdx];
                if (!face.outerLoop.empty()) {
                    Plane plane(face.normal, face.outerLoop[0]);
                    auto [above, below] = split(app.solids[app.selectedSolidIdx], plane);
                    undoStack.pushState("Split");
                    app.solids[app.selectedSolidIdx] = above;
                    addSolid(below, "Split " + (app.selectedSolidIdx < (int)app.solidNames.size()
                        ? app.solidNames[app.selectedSolidIdx] : std::string("Body")));
                    app.planePick = PlanePickMode::None;
                    app.statusText = "Split performed";
                    app.currentTool = Tool::Select;
                }
            }
            return;
        }
    }

    if (app.currentTool == Tool::ConstructionPlane) {
        // Clicking an existing plane selects it rather than making another one,
        // so the offset handle can be grabbed.
        if (app.hoveredConstructionPlaneIdx >= 0) {
            app.selectedConstructionPlaneIdx = app.hoveredConstructionPlaneIdx;
            app.statusText = "Plane selected — drag the arrow to offset it";
            return;
        }
        // With a plane selected the tool is in "adjust this one" mode, so a
        // click that is not on a plane deselects rather than creating another.
        // Previously any drag that missed the offset handle landed on the model
        // and spawned a new plane — every attempt to slide one made a duplicate.
        if (app.selectedConstructionPlaneIdx >= 0) {
            app.selectedConstructionPlaneIdx = -1;
            app.statusText = "Plane deselected — click a face to create another";
            return;
        }

        // Click on face of a solid → create construction plane from that face's normal
        if (app.hoveredFaceIdx >= 0 && app.hoveredSolidIdx >= 0 &&
            app.hoveredSolidIdx < (int)app.solids.size() &&
            app.hoveredFaceIdx < (int)app.solids[app.hoveredSolidIdx].faces.size()) {
            const auto& face = app.solids[app.hoveredSolidIdx].faces[app.hoveredFaceIdx];
            if (!face.outerLoop.empty()) {
                undoStack.pushState("Create plane");
                ConstructionPlaneData cp;
                cp.normal     = face.normal;
                cp.baseOrigin = face.centroid();
                cp.offset     = 0.0f;
                cp.extent     = 15.0f;
                cp.refresh();
                cp.name = "Plane " + std::to_string(app.nextPlaneNumber++);
                app.constructionPlanes.push_back(cp);
                app.selectedConstructionPlaneIdx = (int)app.constructionPlanes.size() - 1;
                animateCameraToPlane(cp.plane, cp.origin);
                app.statusText = "Plane created — drag the arrow to offset it, or use it to Split";
            }
            return;
        }
        // Click on one of the origin planes
        if (app.hoveredOriginPlane >= 0) {
            static const Vec3 normals[3] = {{0,1,0}, {0,0,1}, {1,0,0}};
            static const char* names[3]  = {"XY Plane", "XZ Plane", "YZ Plane"};
            undoStack.pushState("Create plane");
            ConstructionPlaneData cp;
            cp.normal     = normals[app.hoveredOriginPlane];
            cp.baseOrigin = Vec3{0,0,0};
            cp.offset     = 0.0f;
            cp.extent     = 15.0f;
            cp.refresh();
            cp.name   = names[app.hoveredOriginPlane];
            app.nextPlaneNumber++;
            app.constructionPlanes.push_back(cp);
            app.selectedConstructionPlaneIdx = (int)app.constructionPlanes.size() - 1;
            app.hoveredOriginPlane = -1;
            animateCameraToPlane(cp.plane, cp.origin);
            app.statusText = "Construction plane created";
            return;
        }
        return;
    }

    if (app.currentTool == Tool::Join) {
        // Accumulate a selection; the Join button in the panel commits it.
        // Clicking bodies one at a time and having the second click silently
        // fuse them gave no chance to check the pick first.
        if (app.hoveredSolidIdx >= 0 && app.hoveredSolidIdx < (int)app.solids.size()) {
            if (app.selectedSolidSet.count(app.hoveredSolidIdx))
                app.selectedSolidSet.erase(app.hoveredSolidIdx);
            else
                app.selectedSolidSet.insert(app.hoveredSolidIdx);

            app.selectedSolidIdx = app.selectedSolidSet.empty()
                ? -1 : *app.selectedSolidSet.begin();

            size_t n = app.selectedSolidSet.size();
            app.statusText = (n < 2)
                ? "Join — pick at least two bodies (" + std::to_string(n) + " selected)"
                : std::to_string(n) + " bodies selected — press Enter or click Join to merge";
        }
        return;
    }


    if (app.currentTool == Tool::Measure) {
        Ray ray = app.camera.screenToRay(app.mouseX, app.mouseY);
        Vec3 hitPoint;
        bool hit = false;

        for (auto& solid : app.solids) {
            for (auto& face : solid.faces) {
                float t;
                if (rayIntersectFace(ray, face, t)) {
                    hitPoint = ray.at(t);
                    hit = true;
                    break;
                }
            }
            if (hit) break;
        }

        if (!hit) {
            Plane ground({0,1,0}, Vec3{0,0,0});
            float t = ground.intersectRay(ray);
            if (t > 0) { hitPoint = ray.at(t); hit = true; }
        }

        if (hit) {
            if (!app.measureHasFirst) {
                app.measurePointA = hitPoint;
                app.measureHasFirst = true;
                app.statusText = "Measure: click second point";
            } else {
                app.measurePointB = hitPoint;
                float d = measureDistance(app.measurePointA, app.measurePointB);
                char buf[64];
                std::snprintf(buf, sizeof(buf), "Distance: %.2f mm", d);
                app.statusText = buf;
                app.measureHasFirst = false;
            }
        }
    }
}
