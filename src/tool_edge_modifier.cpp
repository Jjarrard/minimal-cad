#include "tool_edge_modifier.h"
#include "geometry.h"
#include "undo.h"

// ---- Internal helpers ----

static std::vector<uint32_t> selectedModifierEdges() {
    std::vector<uint32_t> edges;
    for (int e : app.selectedEdgeSet) {
        if (e >= 0) edges.push_back((uint32_t)e);
    }
    return edges;
}

bool isMouseInEdgeModifierChip() {
    if (!app.edgeModifierChipVisible) return false;
    return app.mouseX >= app.edgeModifierChipX0 && app.mouseX <= app.edgeModifierChipX1 &&
           app.mouseY >= app.edgeModifierChipY0 && app.mouseY <= app.edgeModifierChipY1;
}

bool tryStartEdgeModifierDragFromGizmo() {
    if (!app.edgeModifierGizmoVisible) return false;
    if (!(app.currentTool == Tool::Chamfer || app.currentTool == Tool::Fillet)) return false;

    float value = (app.currentTool == Tool::Chamfer) ? app.chamferDistance : app.filletRadius;
    Vec2 base = app.edgeModifierGizmoBaseScreen;
    Vec2 tip = app.edgeModifierGizmoTipScreen;
    if (base.x <= 0.0f || tip.x <= 0.0f) return false;

    Vec2 mouse = {app.mouseX, app.mouseY};
    Vec2 ab = tip - base;
    float len2 = ab.lengthSq();
    float t = len2 < EPSILON ? 0.0f : std::max(0.0f, std::min(1.0f, (mouse - base).dot(ab) / len2));
    Vec2 proj = base + ab * t;
    float distToArrow = mouse.distTo(proj);
    float distToTip = mouse.distTo(tip);
    float distToBase = mouse.distTo(base);

    if (distToArrow <= 28.0f || distToTip <= 34.0f || distToBase <= 26.0f) {
        app.edgeModifierDragging = true;
        app.edgeModifierDragStartX = app.mouseX;
        app.edgeModifierDragStartY = app.mouseY;
        app.edgeModifierDragStartValue = value;

        if (app.edgeModifierTargetSolidIdx >= 0 &&
            app.edgeModifierTargetSolidIdx < (int)app.solids.size()) {
            app.edgeModifierBackupSolid = app.solids[app.edgeModifierTargetSolidIdx];
            app.edgeModifierPreviewing = true;
            app.edgeModifierGizmoCached = true;
        }

        app.statusText = "Drag arrow to set amount, Enter to apply";
        return true;
    }

    return false;
}

bool tryStartEdgeModifierDragNearSelectedEdges() {
    if (!(app.currentTool == Tool::Chamfer || app.currentTool == Tool::Fillet)) return false;
    if (app.edgeModifierTargetSolidIdx < 0 ||
        app.edgeModifierTargetSolidIdx >= (int)app.solids.size()) return false;
    if (app.selectedEdgeSet.empty()) return false;

    const auto& solid = app.solids[app.edgeModifierTargetSolidIdx];
    Mat4 vp = app.camera.projMatrix() * app.camera.viewMatrix();
    Vec2 mouse = {app.mouseX, app.mouseY};
    float bestDist = 24.0f;

    for (int ei : app.selectedEdgeSet) {
        if (ei < 0 || ei >= (int)solid.edges.size()) continue;
        const auto& edge = solid.edges[ei];
        Vec4 sa = vp * Vec4(edge.start, 1.0f);
        Vec4 sb = vp * Vec4(edge.end, 1.0f);
        if (sa.w <= 0.0f || sb.w <= 0.0f) continue;

        Vec2 a = {(sa.x / sa.w * 0.5f + 0.5f) * app.windowWidth,
                  (1.0f - (sa.y / sa.w * 0.5f + 0.5f)) * app.windowHeight};
        Vec2 b = {(sb.x / sb.w * 0.5f + 0.5f) * app.windowWidth,
                  (1.0f - (sb.y / sb.w * 0.5f + 0.5f)) * app.windowHeight};

        Vec2 ab = b - a;
        float len2 = ab.lengthSq();
        float proj_t = len2 < EPSILON ? 0.0f : std::max(0.0f, std::min(1.0f, (mouse - a).dot(ab) / len2));
        Vec2 proj = a + ab * proj_t;
        bestDist = std::min(bestDist, mouse.distTo(proj));
    }

    if (bestDist > 24.0f) return false;

    float value = (app.currentTool == Tool::Chamfer) ? app.chamferDistance : app.filletRadius;
    app.edgeModifierDragging = true;
    app.edgeModifierDragStartX = app.mouseX;
    app.edgeModifierDragStartY = app.mouseY;
    app.edgeModifierDragStartValue = value;

    if (app.edgeModifierTargetSolidIdx >= 0 &&
        app.edgeModifierTargetSolidIdx < (int)app.solids.size()) {
        app.edgeModifierBackupSolid = app.solids[app.edgeModifierTargetSolidIdx];
        app.edgeModifierPreviewing = true;
        app.edgeModifierGizmoCached = true;
    }

    app.statusText = "Drag to set amount, Enter to apply";
    return true;
}

// ---- Public API ----

void updateEdgeModifierPreview() {
    if (!app.edgeModifierPreviewing) return;
    if (!(app.currentTool == Tool::Chamfer || app.currentTool == Tool::Fillet)) return;
    if (app.edgeModifierTargetSolidIdx < 0 ||
        app.edgeModifierTargetSolidIdx >= (int)app.solids.size()) return;

    auto edges = selectedModifierEdges();
    if (edges.empty()) return;

    float amount = (app.currentTool == Tool::Chamfer) ? app.chamferDistance : app.filletRadius;

    if (app.edgeModifierLastPreviewAmount == -FLT_MAX ||
        std::abs(amount - app.edgeModifierLastPreviewAmount) > 0.01f) {
        app.edgeModifierPreviewSolid = app.edgeModifierBackupSolid;

        if (std::abs(amount) > 1e-4f) {
            try {
                if (app.currentTool == Tool::Chamfer) {
                    app.edgeModifierPreviewSolid = chamfer(app.edgeModifierPreviewSolid, edges, amount);
                } else {
                    app.edgeModifierPreviewSolid = fillet(app.edgeModifierPreviewSolid, edges, amount);
                }
                app.edgeModifierLastPreviewAmount = amount;
            } catch (...) {
                app.edgeModifierPreviewSolid = app.edgeModifierBackupSolid;
                app.edgeModifierLastPreviewAmount = -FLT_MAX;
            }
        } else {
            app.edgeModifierPreviewSolid = app.edgeModifierBackupSolid;
            app.edgeModifierLastPreviewAmount = amount;
        }
    }
}

void clearActiveEdgeModifierSelection() {
    app.selectedEdgeSet.clear();
    app.edgeModifierTargetSolidIdx = -1;
    app.edgeModifierDragging = false;
    app.edgeModifierPreviewing = false;
    app.edgeModifierGizmoCached = false;
    app.edgeModifierLastPreviewAmount = -FLT_MAX;
    app.edgeModifierInputActive = false;
    app.edgeModifierInputFocus = false;
}

void applyActiveEdgeModifier() {
    if (!(app.currentTool == Tool::Chamfer || app.currentTool == Tool::Fillet)) return;
    if (app.edgeModifierTargetSolidIdx < 0 ||
        app.edgeModifierTargetSolidIdx >= (int)app.solids.size()) {
        app.statusText = "Select a body edge or corner first";
        return;
    }

    auto edges = selectedModifierEdges();
    if (edges.empty()) {
        app.statusText = "Select edges or a corner first";
        return;
    }

    float amount = (app.currentTool == Tool::Chamfer) ? app.chamferDistance : app.filletRadius;
    if (std::abs(amount) <= 1e-4f) {
        app.statusText = "Set a non-zero amount";
        return;
    }

    size_t beforeFaceCount = app.solids[app.edgeModifierTargetSolidIdx].faces.size();

    // Always apply fresh from backup to ensure clean geometry
    if (app.edgeModifierPreviewing && app.edgeModifierTargetSolidIdx >= 0 &&
        app.edgeModifierTargetSolidIdx < (int)app.solids.size()) {
        Solid result = app.edgeModifierBackupSolid;
        try {
            if (app.currentTool == Tool::Chamfer) {
                result = chamfer(result, edges, amount);
            } else {
                result = fillet(result, edges, amount);
            }
        } catch (...) {
            app.statusText = "Chamfer/fillet operation failed";
            return;
        }
        // Only push undo and commit after successful computation
        undoStack.pushState((app.currentTool == Tool::Chamfer) ? "Chamfer" : "Fillet");
        app.solids[app.edgeModifierTargetSolidIdx] = result;
    } else {
        Solid result = app.solids[app.edgeModifierTargetSolidIdx];
        try {
            if (app.currentTool == Tool::Chamfer) {
                result = chamfer(result, edges, app.chamferDistance);
            } else {
                result = fillet(result, edges, app.filletRadius);
            }
        } catch (...) {
            app.statusText = "Chamfer/fillet operation failed";
            return;
        }
        undoStack.pushState((app.currentTool == Tool::Chamfer) ? "Chamfer" : "Fillet");
        app.solids[app.edgeModifierTargetSolidIdx] = result;
    }

    size_t afterFaceCount = app.solids[app.edgeModifierTargetSolidIdx].faces.size();
    if (afterFaceCount == beforeFaceCount) {
        app.statusText = "No valid chamfer/fillet edges selected";
    } else {
        app.statusText = (app.currentTool == Tool::Chamfer) ? "Chamfer applied" : "Fillet applied";
    }

    clearActiveEdgeModifierSelection();
}
