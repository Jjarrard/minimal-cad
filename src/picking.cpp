#include "picking.h"
#include <imgui.h>
#include <cmath>

using namespace cad;

bool rayIntersectFace(const Ray& ray, const BRepFace& face, float& outT) {
    if (face.outerLoop.size() < 3) return false;

    Plane plane(face.normal, face.outerLoop[0]);
    float t = plane.intersectRay(ray);
    if (t < 0) return false;

    Vec3 hit = ray.at(t);

    // Choose projection axis (drop the axis most aligned with normal)
    int axis = 0;
    if (std::abs(face.normal.y) > std::abs(face.normal.x) &&
        std::abs(face.normal.y) > std::abs(face.normal.z)) axis = 1;
    else if (std::abs(face.normal.z) > std::abs(face.normal.x)) axis = 2;

    auto project2D = [axis](Vec3 p) -> Vec2 {
        switch (axis) {
            case 0: return {p.y, p.z};
            case 1: return {p.x, p.z};
            case 2: return {p.x, p.y};
        }
        return {p.x, p.y};
    };

    // Point-in-polygon (ray casting)
    Vec2 ph = project2D(hit);
    int n = (int)face.outerLoop.size();
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        Vec2 vi = project2D(face.outerLoop[i]);
        Vec2 vj = project2D(face.outerLoop[j]);
        if ((vi.y > ph.y) != (vj.y > ph.y) &&
            ph.x < (vj.x - vi.x) * (ph.y - vi.y) / (vj.y - vi.y) + vi.x) {
            inside = !inside;
        }
    }

    if (!inside) return false;

    // A hit inside one of the face's holes passes straight through it.
    for (const auto& hole : face.innerLoops) {
        int hn = (int)hole.size();
        if (hn < 3) continue;
        bool inHole = false;
        for (int i = 0, j = hn - 1; i < hn; j = i++) {
            Vec2 vi = project2D(hole[i]);
            Vec2 vj = project2D(hole[j]);
            if ((vi.y > ph.y) != (vj.y > ph.y) &&
                ph.x < (vj.x - vi.x) * (ph.y - vi.y) / (vj.y - vi.y) + vi.x) {
                inHole = !inHole;
            }
        }
        if (inHole) return false;
    }

    outT = t;
    return true;
}

// Ray vs the visible quad of each construction plane.  Needed so a plane can
// be clicked in the viewport — picking one out of the tree is fine for a power
// user but is not how anyone expects to choose a cutting plane.
int pickConstructionPlane(const Ray& ray, float* outT) {
    int best = -1;
    float bestT = 1e18f;
    for (int i = 0; i < (int)app.constructionPlanes.size(); i++) {
        const auto& cp = app.constructionPlanes[i];
        if (!cp.visible) continue;
        float t = cp.plane.intersectRay(ray);
        if (t <= 0.0f || t >= bestT) continue;

        // Inside the drawn square?
        Vec3 hit = ray.at(t);
        Vec3 n = cp.plane.normal;
        Vec3 ref = (std::abs(n.y) > 0.9f) ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        Vec3 u = n.cross(ref).normalized();
        Vec3 v = n.cross(u).normalized();
        Vec3 d = hit - cp.origin;
        if (std::abs(d.dot(u)) <= cp.extent && std::abs(d.dot(v)) <= cp.extent) {
            bestT = t;
            best = i;
        }
    }
    if (outT) *outT = bestT;
    return best;
}

void updatePicking() {
    if (ImGui::GetIO().WantCaptureMouse) {
        app.hoveredSolidIdx = -1;
        app.hoveredFaceIdx = -1;
        app.hoveredEdgeIdx = -1;
    app.hoveredEdgeSolidIdx = -1;
        app.hoveredCornerSolidIdx = -1;
        app.hoveredCornerValid = false;
        app.hoveredConstructionPlaneIdx = -1;
        return;
    }

    Ray ray = app.camera.screenToRay(app.mouseX, app.mouseY);

    app.hoveredSolidIdx = -1;
    app.hoveredFaceIdx = -1;
    app.hoveredEdgeIdx = -1;
    app.hoveredEdgeSolidIdx = -1;
    app.hoveredCornerSolidIdx = -1;
    app.hoveredCornerValid = false;
    float planeT = 1e18f;
    app.hoveredConstructionPlaneIdx = pickConstructionPlane(ray, &planeT);
    float bestT = 1e18f;

    // Face picking
    for (int si = 0; si < (int)app.solids.size(); si++) {
        for (int fi = 0; fi < (int)app.solids[si].faces.size(); fi++) {
            float t;
            if (rayIntersectFace(ray, app.solids[si].faces[fi], t) && t < bestT) {
                bestT = t;
                app.hoveredSolidIdx = si;
                app.hoveredFaceIdx = fi;
            }
        }
    }

    // A plane sitting behind a solid should not light up through it.
    if (app.hoveredConstructionPlaneIdx >= 0 && bestT < planeT) {
        app.hoveredConstructionPlaneIdx = -1;
    }

    // Edge picking (screen-space distance)
    float bestEdgeDist = (app.currentTool == Tool::Chamfer || app.currentTool == Tool::Fillet) ? 14.0f : 5.0f;
    for (int si = 0; si < (int)app.solids.size(); si++) {
        for (int ei = 0; ei < (int)app.solids[si].edges.size(); ei++) {
            const auto& edge = app.solids[si].edges[ei];
            Mat4 vp = app.camera.projMatrix() * app.camera.viewMatrix();
            Vec4 sa = vp * Vec4(edge.start, 1.0f);
            Vec4 sb = vp * Vec4(edge.end, 1.0f);
            if (sa.w < 0.001f || sb.w < 0.001f) continue;
            Vec2 screenA = {(sa.x/sa.w * 0.5f + 0.5f) * app.camera.viewportWidth,
                           (0.5f - sa.y/sa.w * 0.5f) * app.camera.viewportHeight};
            Vec2 screenB = {(sb.x/sb.w * 0.5f + 0.5f) * app.camera.viewportWidth,
                           (0.5f - sb.y/sb.w * 0.5f) * app.camera.viewportHeight};
            Vec2 mouse = {app.mouseX, app.mouseY};

            Vec2 ab = screenB - screenA;
            float len2 = ab.lengthSq();
            float t = len2 < EPSILON ? 0.0f : std::max(0.0f, std::min(1.0f, (mouse - screenA).dot(ab) / len2));
            Vec2 proj = screenA + ab * t;
            float d = mouse.distTo(proj);

            if (d < bestEdgeDist) {
                bestEdgeDist = d;
                app.hoveredEdgeSolidIdx = si;
                app.hoveredEdgeIdx = ei;
            }
        }
    }

    // Corner picking (expanded safe area for chamfer/fillet)
    if (app.currentTool == Tool::Chamfer || app.currentTool == Tool::Fillet) {
        float bestCornerDist = 18.0f;
        Mat4 vp = app.camera.projMatrix() * app.camera.viewMatrix();
        Vec2 mouse = {app.mouseX, app.mouseY};

        for (int si = 0; si < (int)app.solids.size(); si++) {
            std::vector<Vec3> corners;
            for (const auto& edge : app.solids[si].edges) {
                auto appendUnique = [&](Vec3 p) {
                    for (auto& existing : corners) {
                        if (existing.distTo(p) < 0.0001f) return;
                    }
                    corners.push_back(p);
                };
                appendUnique(edge.start);
                appendUnique(edge.end);
            }

            for (const auto& corner : corners) {
                Vec4 sp = vp * Vec4(corner, 1.0f);
                if (sp.w <= 0.0f) continue;
                Vec2 screen = {
                    (sp.x / sp.w * 0.5f + 0.5f) * app.camera.viewportWidth,
                    (0.5f - sp.y / sp.w * 0.5f) * app.camera.viewportHeight
                };
                float d = mouse.distTo(screen);
                if (d < bestCornerDist) {
                    bestCornerDist = d;
                    app.hoveredCornerSolidIdx = si;
                    app.hoveredCornerPos = corner;
                    app.hoveredCornerValid = true;
                }
            }
        }
    }
}
