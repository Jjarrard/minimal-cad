#include "extrude.h"
#include "csg.h"
#include "input.h"
#include "undo.h"

#include <cmath>
#include <string>

using namespace cad;

static const float EPS = 1e-4f;

// Ensure extrude always gets positive distance (flip dir if negative)
static void normDir(Vec3& d, float& dist) {
    if (dist < 0.0f) { d = -d; dist = -dist; }
}

// Helper: build preview solids from current extrude state
static void buildExtrudePreviewSolids(Vec3 dir) {
    app.extrudePreviewSolids.clear();

    bool isComplement = (app.extrudeSelectedProfile == -2);

    if (isComplement && !app.sketchSourceFaceLoop.empty()) {
        auto allProfiles = app.sketch.extractProfiles();
        auto doExtrude = [&](Vec3 d, float dist) {
            normDir(d, dist);
            if (dist < 0.001f) return;
            app.extrudePreviewSolids.push_back(
                extrudeWithHoles(app.sketchSourceFaceLoop, allProfiles, d, dist));
        };
        switch (app.extrudeDir) {
            case ExtrudeDirection::OneSide:    doExtrude(dir, app.extrudeDistance); break;
            case ExtrudeDirection::Symmetric:
                doExtrude(dir, app.extrudeDistance * 0.5f);
                doExtrude(-dir, app.extrudeDistance * 0.5f);
                break;
            case ExtrudeDirection::TwoSides:
                doExtrude(dir, app.extrudeDistance);
                doExtrude(-dir, app.extrudeDistance2);
                break;
        }
    } else {
        for (auto& profile : app.extrudeProfiles) {
            auto doExtrude = [&](Vec3 d, float dist) {
                normDir(d, dist);
                if (dist < 0.001f) return;
                app.extrudePreviewSolids.push_back(
                    app.extrudeFaceHoles.empty()
                        ? extrude(profile, d, dist)
                        : extrudeWithHoles(profile, app.extrudeFaceHoles, d, dist));
            };
            switch (app.extrudeDir) {
                case ExtrudeDirection::OneSide:
                    doExtrude(dir, app.extrudeDistance);
                    break;
                case ExtrudeDirection::Symmetric:
                    doExtrude(dir, app.extrudeDistance * 0.5f);
                    doExtrude(-dir, app.extrudeDistance * 0.5f);
                    break;
                case ExtrudeDirection::TwoSides:
                    doExtrude(dir, app.extrudeDistance);
                    doExtrude(-dir, app.extrudeDistance2);
                    break;
            }
        }
    }
}

void enterExtrudeMode() {
    // Discard any in-progress (uncommitted) line segment
    app.sketchDrawing = false;
    app.dimInput.reset();

    // Exit sketch mode
    if (app.inSketchMode) {
        app.inSketchMode = false;
    }

    app.currentTool = Tool::Extrude;
    app.extrudePreviewing = true;
    app.extrudeDragging = false;
    app.extrudeDistance = 0.0f;
    app.extrudeBoolManual = false;
    app.extrudeProfiles.clear();
    app.extrudePreviewSolids.clear();
    app.extrudeFaceSolidIdx = -1;
    app.extrudeSelectedProfile = -1;
    app.extrudeInputActive = true;
    app.extrudeInputFocus = true;
    app.extrudeInputBuf[0] = '\0';
    updateExtrudePreview();
    if (!app.extrudeProfiles.empty()) {
        app.statusText = "Extrude — type distance or drag arrow, Enter to confirm";
    } else {
        app.statusText = "Extrude — click a face to extrude";
    }
}

void updateExtrudePreview() {
    app.extrudePreviewSolids.clear();

    if (app.currentTool != Tool::Extrude) return;

    // If no profiles cached yet, try sketch profiles
    if (app.extrudeProfiles.empty() && !app.sketch.entities.empty()) {
        auto allProfiles = app.sketch.extractProfiles();
        if (!allProfiles.empty()) {
            app.extrudeProfiles = allProfiles;
        }
    }

    // Determine extrude direction
    Vec3 dir;
    if (app.extrudeDragging && app.extrudeFaceSolidIdx >= 0) {
        dir = app.extrudeArrowDir;
    } else if (!app.sketch.entities.empty()) {
        dir = app.sketch.sketchPlane.normal;
    } else {
        dir = {0, 1, 0};
    }

    // Update arrow base for sketch profiles
    if (!app.extrudeDragging && !app.extrudeProfiles.empty()) {
        Vec3 center{0,0,0};
        for (auto& p : app.extrudeProfiles[0]) center += p;
        if (!app.extrudeProfiles[0].empty())
            center = center * (1.0f / app.extrudeProfiles[0].size());
        app.extrudeArrowBase = center;
        app.extrudeArrowDir = dir;
    }

    buildExtrudePreviewSolids(dir);

    // Auto-cut: check if the extrude preview overlaps any existing solid
    if (!app.extrudeBoolManual && !app.extrudePreviewSolids.empty()) {
        bool overlaps = false;
        for (auto& preview : app.extrudePreviewSolids) {
            AABB pb = preview.bounds();
            for (int i = 0; i < (int)app.solids.size(); i++) {
                if (!app.solids[i].visible) continue;
                AABB sb = app.solids[i].bounds();
                // AABB overlap test (with small tolerance)
                if (pb.min.x < sb.max.x - EPS && pb.max.x > sb.min.x + EPS &&
                    pb.min.y < sb.max.y - EPS && pb.max.y > sb.min.y + EPS &&
                    pb.min.z < sb.max.z - EPS && pb.max.z > sb.min.z + EPS) {
                    overlaps = true;
                    break;
                }
            }
            if (overlaps) break;
        }
        app.extrudeBoolOp = overlaps ? ExtrudeBoolOp::Cut : ExtrudeBoolOp::NewBody;
    }
}

void performExtrude() {
    if (app.extrudeProfiles.empty() && app.extrudeSelectedProfile != -2) {
        app.statusText = "Extrude failed: no sketch profiles";
        return;
    }
    if (app.extrudeSelectedProfile == -2 && app.sketchSourceFaceLoop.empty()) {
        app.statusText = "Extrude failed: no face boundary";
        return;
    }

    undoStack.pushState("Extrude");

    Vec3 dir = app.extrudeArrowDir;
    if (!app.extrudeDragging && !app.sketch.entities.empty()) {
        dir = app.sketch.sketchPlane.normal;
    }

    bool fromFace = (app.extrudeFaceSolidIdx >= 0);
    bool isComplement = (app.extrudeSelectedProfile == -2);

    // Find target solid for boolean operations
    int target = fromFace ? app.extrudeFaceSolidIdx : app.extrudeTargetSolidIdx;
    if (target < 0) target = app.sketchSourceSolidIdx;

    // Auto-find target: if cutting/joining and no explicit target, find overlapping solid
    if (target < 0 && app.extrudeBoolOp != ExtrudeBoolOp::NewBody &&
        !app.extrudePreviewSolids.empty()) {
        AABB pb = app.extrudePreviewSolids[0].bounds();
        float bestOverlap = 0;
        for (int i = 0; i < (int)app.solids.size(); i++) {
            if (!app.solids[i].visible) continue;
            AABB sb = app.solids[i].bounds();
            float ox = std::max(0.0f, std::min(pb.max.x, sb.max.x) - std::max(pb.min.x, sb.min.x));
            float oy = std::max(0.0f, std::min(pb.max.y, sb.max.y) - std::max(pb.min.y, sb.min.y));
            float oz = std::max(0.0f, std::min(pb.max.z, sb.max.z) - std::max(pb.min.z, sb.min.z));
            float vol = ox * oy * oz;
            if (vol > bestOverlap) { bestOverlap = vol; target = i; }
        }
    }
    bool hasTarget = target >= 0 && target < (int)app.solids.size();

    auto addSolidToScene = [&](Solid s) {
        if (s.faces.empty()) {
            app.statusText = "Extrude produced empty geometry — check sketch profile";
            return;
        }
        switch (app.extrudeBoolOp) {
        case ExtrudeBoolOp::NewBody:
            addSolid(s);
            break;
        case ExtrudeBoolOp::Join:
            if (hasTarget)
                app.solids[target] = csgUnion(app.solids[target], s);
            else addSolid(s);
            break;
        case ExtrudeBoolOp::Cut:
            if (hasTarget)
                app.solids[target] = csgSubtract(app.solids[target], s);
            else addSolid(s);
            break;
        case ExtrudeBoolOp::Intersect:
            if (hasTarget)
                app.solids[target] = csgIntersect(app.solids[target], s);
            else addSolid(s);
            break;
        }
    };

    if (isComplement && !app.sketchSourceFaceLoop.empty()) {
        if (app.sketchSourceSolidIdx >= 0 &&
            (app.sketchSourceSolidIdx >= (int)app.solids.size() ||
             app.sketchSourceFaceIdx < 0 ||
             app.sketchSourceFaceIdx >= (int)app.solids[app.sketchSourceSolidIdx].faces.size())) {
            app.statusText = "Source face no longer valid";
            return;
        }
        auto allProfiles = app.sketch.extractProfiles();
        auto doExtrude = [&](Vec3 d, float dist) {
            normDir(d, dist);
            if (dist < 0.001f) return;
            Solid s = extrudeWithHoles(app.sketchSourceFaceLoop, allProfiles, d, dist);
            addSolidToScene(s);
        };
        switch (app.extrudeDir) {
            case ExtrudeDirection::OneSide:    doExtrude(dir, app.extrudeDistance); break;
            case ExtrudeDirection::Symmetric:
                doExtrude(dir, app.extrudeDistance * 0.5f);
                doExtrude(-dir, app.extrudeDistance * 0.5f);
                break;
            case ExtrudeDirection::TwoSides:
                doExtrude(dir, app.extrudeDistance);
                doExtrude(-dir, app.extrudeDistance2);
                break;
        }
        app.statusText = "Extruded face with hole(s)";
    } else {
        for (auto& profile : app.extrudeProfiles) {
            auto doExtrude = [&](Vec3 d, float dist) {
                normDir(d, dist);
                if (dist < 0.001f) return;
                Solid s = app.extrudeFaceHoles.empty()
                    ? extrude(profile, d, dist)
                    : extrudeWithHoles(profile, app.extrudeFaceHoles, d, dist);
                addSolidToScene(s);
            };
            switch (app.extrudeDir) {
                case ExtrudeDirection::OneSide:    doExtrude(dir, app.extrudeDistance); break;
                case ExtrudeDirection::Symmetric:
                    doExtrude(dir, app.extrudeDistance * 0.5f);
                    doExtrude(-dir, app.extrudeDistance * 0.5f);
                    break;
                case ExtrudeDirection::TwoSides:
                    doExtrude(dir, app.extrudeDistance);
                    doExtrude(-dir, app.extrudeDistance2);
                    break;
            }
        }
        app.statusText = "Extruded " + std::to_string(app.extrudeProfiles.size()) + " profile(s)";
    }

    if (!fromFace && !app.sketch.entities.empty()) {
        app.sketch.clear();
        app.inSketchMode = false;
    }

    // Reset all extrude state
    app.extrudePreviewing = false;
    app.extrudeDragging = false;
    app.extrudePreviewSolids.clear();
    app.extrudeProfiles.clear();
    app.extrudeFaceSolidIdx = -1;
    app.extrudeFaceIdx = -1;
    app.extrudeFaceProfile.clear();
    app.extrudeFaceHoles.clear();
    app.extrudeSelectedProfile = -1;
    app.extrudeInputActive = false;
    app.extrudeInputBuf[0] = '\0';
    app.sketchSourceSolidIdx = -1;
    app.sketchSourceFaceIdx = -1;
    app.sketchSourceFaceLoop.clear();
    app.currentTool = Tool::Select;
}

// ---- Extrude arrow drag ----
void handleExtrudeDrag(float dy) {
    if (!app.extrudeDragging) return;

    Mat4 vp = app.camera.projMatrix() * app.camera.viewMatrix();
    auto project = [&](Vec3 p) -> Vec2 {
        Vec4 clip = vp * Vec4(p, 1.0f);
        if (clip.w <= 0) return {0, 0};
        return {(clip.x / clip.w * 0.5f + 0.5f) * app.windowWidth,
                (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * app.windowHeight};
    };

    Vec2 baseScreen = project(app.extrudeArrowBase);
    Vec2 dirScreen = project(app.extrudeArrowBase + app.extrudeArrowDir) - baseScreen;
    float dirLen = dirScreen.length();
    if (dirLen < 1.0f) return;
    Vec2 dirNorm = dirScreen * (1.0f / dirLen);

    Vec2 mouseDelta = {app.mouseX - app.extrudeDragStartX, app.mouseY - app.extrudeDragStartY};
    float projected = mouseDelta.dot(dirNorm);

    float scale = app.camera.distance * 0.005f * (dirLen > 10.0f ? 1.0f / (dirLen * 0.02f) : 0.5f);
    app.extrudeDistance = app.extrudeDragStartDist + projected * scale;
    updateExtrudePreview();
}

void handleExtrudeRelease() {
    if (!app.extrudeDragging) return;
    app.extrudeDragging = false;
    if (std::abs(app.extrudeDistance) < 0.1f) app.extrudeDistance = 0.0f;
    app.statusText = "Adjust distance, Enter to confirm";
}

bool setExtrudeDistanceFromFace(int solidIndex, int faceIndex) {
    if (solidIndex < 0 || solidIndex >= (int)app.solids.size()) return false;
    if (faceIndex < 0 || faceIndex >= (int)app.solids[solidIndex].faces.size()) return false;

    const auto& face = app.solids[solidIndex].faces[faceIndex];
    if (face.outerLoop.empty()) return false;

    Vec3 dir = app.extrudeArrowDir.normalized();
    if (dir.lengthSq() < EPSILON) return false;

    Vec3 planePoint = face.outerLoop[0];
    float denom = face.normal.dot(dir);
    if (std::abs(denom) < EPSILON) return false;

    float distance = (planePoint - app.extrudeArrowBase).dot(face.normal) / denom;
    if (std::abs(distance) < 0.01f) return false;

    app.extrudeDistance = distance;
    updateExtrudePreview();
    app.statusText = "Extrude extent snapped to face plane, Enter to confirm";
    return true;
}
