#include "sketch.h"
#include "geometry.h"
#include <cmath>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>

namespace cad {

static int circleSegmentCount(float radius) {
    int segs = (int)std::ceil(radius * 2.5f);
    return std::max(128, std::min(720, segs));
}

void Sketch::setup(Plane plane, Vec3 org) {
    sketchPlane = plane;
    origin = org;

    // Compute local axes on the plane
    Vec3 n = plane.normal;
    // Pick a reference vector not parallel to normal
    Vec3 ref = std::abs(n.dot(Vec3{0,1,0})) < 0.99f ? Vec3{0,1,0} : Vec3{1,0,0};
    axisU = ref.cross(n).normalized();
    axisV = n.cross(axisU).normalized();
}

Vec2 Sketch::worldToSketch(Vec3 worldPos) const {
    Vec3 local = worldPos - origin;
    return {local.dot(axisU), local.dot(axisV)};
}

Vec3 Sketch::sketchToWorld(Vec2 sketchPos) const {
    return origin + axisU * sketchPos.x + axisV * sketchPos.y;
}

void Sketch::setReferenceLoops(const std::vector<std::vector<Vec3>>& loops3D) {
    referenceLoops.clear();
    for (const auto& loop : loops3D) {
        if (loop.size() < 2) continue;
        std::vector<Vec2> flat;
        flat.reserve(loop.size());
        for (const Vec3& p : loop) flat.push_back(worldToSketch(p));
        referenceLoops.push_back(std::move(flat));
    }
}

SnapResult Sketch::findSnap(Vec2 cursor, bool hasFrom, Vec2 fromPoint) const {
    SnapResult best;
    best.distance = snapDistance;

    // Ranking weight per snap type — lower wins.  Nearest-point alone is the
    // wrong rule: the perpendicular distance to an edge is always shorter than
    // the distance to that edge's own corner, so a pure distance test can never
    // let you snap to a corner.  Meaningful points therefore get a discount,
    // and the grid is weakest so real geometry always beats it.
    auto priority = [](SnapType t) -> float {
        switch (t) {
            case SnapType::Origin:       return 0.45f;
            case SnapType::Endpoint:     return 0.50f;
            case SnapType::Center:       return 0.55f;
            case SnapType::Intersection: return 0.60f;
            case SnapType::Midpoint:     return 0.65f;
            case SnapType::Perpendicular:
            case SnapType::Parallel:     return 0.85f;
            case SnapType::Horizontal:
            case SnapType::Vertical:     return 0.90f;
            case SnapType::OnLine:       return 1.00f;
            case SnapType::Grid:         return 1.40f;
            default:                     return 1.00f;
        }
    };

    float bestScore = 1e18f;
    auto trySnap = [&](Vec2 p, SnapType type, Vec2 refDir = {0,0}) {
        float d = cursor.distTo(p);
        if (d > snapDistance) return;
        float score = d * priority(type);
        if (score < bestScore) {
            bestScore = score;
            best = {type, p, d, refDir};
        }
    };

    // Snap to origin (extra magnetic)
    float originDist = cursor.distTo({0, 0});
    if (originDist < snapDistance * 3.0f) {
        trySnap({0, 0}, SnapType::Origin);
    }

    // Snap to grid
    if (snapToGrid) {
        Vec2 gridPt = {
            std::round(cursor.x / gridSize) * gridSize,
            std::round(cursor.y / gridSize) * gridSize,
        };
        trySnap(gridPt, SnapType::Grid);
    }

    // Snap to the face being sketched on: its corners, edge midpoints, the
    // edges themselves, and the face centre.  This is what makes drawing on an
    // existing face feel magnetic instead of freehand.
    for (const auto& loop : referenceLoops) {
        if (loop.size() < 2) continue;

        Vec2 centre{0, 0};
        for (const Vec2& p : loop) centre = centre + p;
        centre = centre * (1.0f / (float)loop.size());
        trySnap(centre, SnapType::Center);

        for (size_t i = 0; i < loop.size(); i++) {
            Vec2 a = loop[i];
            Vec2 b = loop[(i + 1) % loop.size()];
            trySnap(a, SnapType::Endpoint);
            trySnap((a + b) * 0.5f, SnapType::Midpoint);

            Vec2 ab = b - a;
            float len2 = ab.lengthSq();
            if (len2 > EPSILON) {
                float t = std::max(0.0f, std::min(1.0f, (cursor - a).dot(ab) / len2));
                Vec2 closest = a + ab * t;
                if (cursor.distTo(closest) < snapDistance * 0.8f && t > 0.01f && t < 0.99f) {
                    trySnap(closest, SnapType::OnLine, ab.normalized());
                }
            }
        }
    }

    // Snap to entity features
    for (const auto& e : entities) {
        switch (e.type) {
        case SketchEntityType::Line: {
            trySnap(e.p0, SnapType::Endpoint);
            trySnap(e.p1, SnapType::Endpoint);
            trySnap((e.p0 + e.p1) * 0.5f, SnapType::Midpoint);

            // On-line snap (closest point on the line segment)
            Vec2 ab = e.p1 - e.p0;
            float len2 = ab.lengthSq();
            if (len2 > EPSILON) {
                float t = std::max(0.0f, std::min(1.0f, (cursor - e.p0).dot(ab) / len2));
                Vec2 closest = e.p0 + ab * t;
                float d = cursor.distTo(closest);
                if (d < snapDistance * 0.8f && t > 0.01f && t < 0.99f) {
                    trySnap(closest, SnapType::OnLine, ab.normalized());
                }
            }
            break;
        }
        case SketchEntityType::Circle:
            trySnap(e.p0, SnapType::Center);
            // Cardinal + quadrant snaps (8 points around circle)
            for (int q = 0; q < 8; q++) {
                float ang = q * (PI / 4.0f);
                trySnap(e.p0 + Vec2{std::cos(ang) * e.radius, std::sin(ang) * e.radius}, SnapType::Endpoint);
            }
            break;

        case SketchEntityType::Rectangle:
            trySnap(e.p0, SnapType::Endpoint);
            trySnap(e.p1, SnapType::Endpoint);
            trySnap({e.p0.x, e.p1.y}, SnapType::Endpoint);
            trySnap({e.p1.x, e.p0.y}, SnapType::Endpoint);
            trySnap((e.p0 + e.p1) * 0.5f, SnapType::Center);
            // On-line for rect edges
            {
                Vec2 corners[4] = {e.p0, {e.p1.x, e.p0.y}, e.p1, {e.p0.x, e.p1.y}};
                for (int j = 0; j < 4; j++) {
                    Vec2 a = corners[j], b = corners[(j+1)%4];
                    Vec2 ab = b - a;
                    float len2 = ab.lengthSq();
                    if (len2 > EPSILON) {
                        float t = std::max(0.0f, std::min(1.0f, (cursor - a).dot(ab) / len2));
                        Vec2 closest = a + ab * t;
                        if (cursor.distTo(closest) < snapDistance * 0.8f && t > 0.01f && t < 0.99f) {
                            trySnap(closest, SnapType::OnLine, ab.normalized());
                        }
                    }
                }
            }
            break;

        case SketchEntityType::Arc:
            trySnap(e.p0, SnapType::Center);
            trySnap(e.p1, SnapType::Endpoint);
            trySnap(e.p2, SnapType::Endpoint);
            break;
        }
    }

    // Line-line intersection snaps
    {
        struct Seg { Vec2 a, b; };
        std::vector<Seg> segs;
        for (const auto& e : entities) {
            if (e.type == SketchEntityType::Line) {
                segs.push_back({e.p0, e.p1});
            } else if (e.type == SketchEntityType::Rectangle) {
                Vec2 c[4] = {e.p0, {e.p1.x, e.p0.y}, e.p1, {e.p0.x, e.p1.y}};
                for (int j = 0; j < 4; j++) segs.push_back({c[j], c[(j+1)%4]});
            }
        }
        for (size_t i = 0; i < segs.size(); i++) {
            for (size_t j = i + 1; j < segs.size(); j++) {
                Vec2 d1 = segs[i].b - segs[i].a;
                Vec2 d2 = segs[j].b - segs[j].a;
                float cross = d1.x * d2.y - d1.y * d2.x;
                if (std::abs(cross) < EPSILON) continue;
                Vec2 diff = segs[j].a - segs[i].a;
                float t = (diff.x * d2.y - diff.y * d2.x) / cross;
                float u = (diff.x * d1.y - diff.y * d1.x) / cross;
                if (t >= -0.001f && t <= 1.001f && u >= -0.001f && u <= 1.001f) {
                    Vec2 pt = segs[i].a + d1 * t;
                    trySnap(pt, SnapType::Intersection);
                }
            }
        }
    }

    // Horizontal/vertical constraint from draw start point
    if (hasFrom) {
        Vec2 delta = cursor - fromPoint;
        if (std::abs(delta.y) < snapDistance * 1.5f && std::abs(delta.x) > snapDistance) {
            trySnap({cursor.x, fromPoint.y}, SnapType::Horizontal);
        }
        if (std::abs(delta.x) < snapDistance * 1.5f && std::abs(delta.y) > snapDistance) {
            trySnap({fromPoint.x, cursor.y}, SnapType::Vertical);
        }
    }

    // Alignment to existing endpoints (horizontal/vertical)
    for (const auto& e : entities) {
        if (e.type == SketchEntityType::Line || e.type == SketchEntityType::Rectangle) {
            for (const Vec2* p : {&e.p0, &e.p1}) {
                if (std::abs(cursor.x - p->x) < snapDistance * 0.3f)
                    trySnap({p->x, cursor.y}, SnapType::Vertical);
                if (std::abs(cursor.y - p->y) < snapDistance * 0.3f)
                    trySnap({cursor.x, p->y}, SnapType::Horizontal);
            }
        }
    }

    // Parallel and perpendicular to existing lines (from draw start)
    if (hasFrom) {
        Vec2 drawDir = cursor - fromPoint;
        float drawLen = drawDir.length();
        if (drawLen > EPSILON * 10) {
            Vec2 drawDirN = drawDir / drawLen;

            for (const auto& e : entities) {
                if (e.type != SketchEntityType::Line) continue;
                Vec2 lineDir = (e.p1 - e.p0).normalized();

                // Parallel: drawing direction aligns with existing line
                float dotPar = std::abs(drawDirN.dot(lineDir));
                if (dotPar > 0.998f) {
                    Vec2 proj = fromPoint + lineDir * drawDir.dot(lineDir);
                    if (cursor.distTo(proj) < snapDistance * 2.0f) {
                        trySnap(proj, SnapType::Parallel, lineDir);
                    }
                    Vec2 negDir = lineDir * -1.0f;
                    Vec2 proj2 = fromPoint + negDir * drawDir.dot(negDir);
                    if (cursor.distTo(proj2) < snapDistance * 2.0f) {
                        trySnap(proj2, SnapType::Parallel, lineDir);
                    }
                }

                // Perpendicular: drawing direction is 90° to existing line
                float dotPerp = std::abs(drawDirN.dot(lineDir.perp()));
                if (dotPerp > 0.998f) {
                    Vec2 perpDir = lineDir.perp();
                    Vec2 proj = fromPoint + perpDir * drawDir.dot(perpDir);
                    if (cursor.distTo(proj) < snapDistance * 2.0f) {
                        trySnap(proj, SnapType::Perpendicular, lineDir);
                    }
                    Vec2 negPerp = perpDir * -1.0f;
                    Vec2 proj2 = fromPoint + negPerp * drawDir.dot(negPerp);
                    if (cursor.distTo(proj2) < snapDistance * 2.0f) {
                        trySnap(proj2, SnapType::Perpendicular, lineDir);
                    }
                }
            }
        }
    }

    return best;
}

void Sketch::addLine(Vec2 a, Vec2 b) {
    if (a.distTo(b) < EPSILON) return;
    entities.push_back({SketchEntityType::Line, a, b, {}, 0, false, false});
}

void Sketch::addCircle(Vec2 center, float radius) {
    if (radius < EPSILON) return;
    entities.push_back({SketchEntityType::Circle, center, {}, {}, radius, false, false});
}

void Sketch::addRectangle(Vec2 corner1, Vec2 corner2) {
    if (std::abs(corner2.x - corner1.x) < EPSILON || std::abs(corner2.y - corner1.y) < EPSILON) return;
    entities.push_back({SketchEntityType::Rectangle, corner1, corner2, {}, 0, false, false});
}

void Sketch::addArc(Vec2 center, Vec2 start, Vec2 end, float radius) {
    entities.push_back({SketchEntityType::Arc, center, start, end, radius, false, false});
}

std::vector<std::vector<Vec3>> Sketch::extractProfiles() const {
    std::vector<std::vector<Vec3>> profiles;

    // Collect line segments (from Line entities and Rectangle edges)
    struct Seg { Vec2 a, b; bool used = false; };
    std::vector<Seg> segments;

    for (const auto& e : entities) {
        if (e.construction) continue;

        switch (e.type) {
        case SketchEntityType::Line:
            segments.push_back({e.p0, e.p1, false});
            break;
        case SketchEntityType::Rectangle: {
            Vec2 c0 = e.p0;
            Vec2 c1 = {e.p1.x, e.p0.y};
            Vec2 c2 = e.p1;
            Vec2 c3 = {e.p0.x, e.p1.y};
            segments.push_back({c0, c1, false});
            segments.push_back({c1, c2, false});
            segments.push_back({c2, c3, false});
            segments.push_back({c3, c0, false});
            break;
        }
        case SketchEntityType::Circle: {
            // Circle is always a closed profile
            int segs = circleSegmentCount(e.radius);
            std::vector<Vec2> pts2d;
            for (int i = 0; i < segs; i++) {
                float angle = (float)i / segs * 2.0f * PI;
                pts2d.push_back(e.p0 + Vec2{std::cos(angle) * e.radius, std::sin(angle) * e.radius});
            }
            std::vector<Vec3> profile3d;
            for (const auto& p : pts2d)
                profile3d.push_back(sketchToWorld(p));
            profiles.push_back(profile3d);
            break;
        }
        default:
            break;
        }
    }

    // Find closed loops from line segments
    const float tol = snapDistance * 2.0f;

    auto pointsMatch = [&](Vec2 a, Vec2 b) -> bool {
        return a.distTo(b) < tol;
    };

    // Try to build closed chains
    for (int startIdx = 0; startIdx < (int)segments.size(); startIdx++) {
        if (segments[startIdx].used) continue;

        // Try to build a loop starting from this segment
        std::vector<Vec2> chain;
        chain.push_back(segments[startIdx].a);
        chain.push_back(segments[startIdx].b);

        std::vector<bool> localUsed(segments.size(), false);
        localUsed[startIdx] = true;

        bool extended = true;
        while (extended) {
            extended = false;
            Vec2 tip = chain.back();

            // Check if we've closed the loop
            if (chain.size() >= 3 && pointsMatch(tip, chain.front())) {
                chain.pop_back(); // remove duplicate closing point
                // Mark all used segments
                for (int i = 0; i < (int)segments.size(); i++) {
                    if (localUsed[i]) segments[i].used = true;
                }
                // Convert to 3D profile
                std::vector<Vec3> profile3d;
                for (const auto& p : chain)
                    profile3d.push_back(sketchToWorld(p));
                if (profile3d.size() >= 3) {
                    profiles.push_back(profile3d);
                }
                break;
            }

            // Find next connected segment
            for (int i = 0; i < (int)segments.size(); i++) {
                if (localUsed[i]) continue;
                if (pointsMatch(tip, segments[i].a)) {
                    chain.push_back(segments[i].b);
                    localUsed[i] = true;
                    extended = true;
                    break;
                }
                if (pointsMatch(tip, segments[i].b)) {
                    chain.push_back(segments[i].a);
                    localUsed[i] = true;
                    extended = true;
                    break;
                }
            }
        }
    }

    return profiles;
}

void Sketch::deleteSelected() {
    entities.erase(
        std::remove_if(entities.begin(), entities.end(),
            [](const SketchEntity& e) { return e.selected; }),
        entities.end());
}

int Sketch::pickEntity(Vec2 point, float tolerance) const {
    for (int i = 0; i < (int)entities.size(); i++) {
        const auto& e = entities[i];
        switch (e.type) {
        case SketchEntityType::Line: {
            // Distance from point to line segment
            Vec2 ab = e.p1 - e.p0;
            float t = ab.lengthSq();
            if (t < EPSILON) { if (point.distTo(e.p0) < tolerance) return i; break; }
            t = std::max(0.0f, std::min(1.0f, (point - e.p0).dot(ab) / t));
            Vec2 proj = e.p0 + ab * t;
            if (point.distTo(proj) < tolerance) return i;
            break;
        }
        case SketchEntityType::Circle: {
            float d = point.distTo(e.p0);
            if (std::abs(d - e.radius) < tolerance) return i;
            break;
        }
        case SketchEntityType::Rectangle: {
            // Check 4 edges
            Vec2 corners[4] = {e.p0, {e.p1.x, e.p0.y}, e.p1, {e.p0.x, e.p1.y}};
            for (int j = 0; j < 4; j++) {
                Vec2 a = corners[j], b = corners[(j+1)%4];
                Vec2 ab = b - a;
                float len2 = ab.lengthSq();
                if (len2 < EPSILON) continue;
                float t = std::max(0.0f, std::min(1.0f, (point - a).dot(ab) / len2));
                Vec2 proj = a + ab * t;
                if (point.distTo(proj) < tolerance) return i;
            }
            break;
        }
        case SketchEntityType::Arc: {
            float d = point.distTo(e.p0);
            if (std::abs(d - e.radius) < tolerance) return i;
            break;
        }
        }
    }
    return -1;
}

void Sketch::clear() {
    entities.clear();
}

std::vector<std::vector<Vec2>> Sketch::extractProfiles2D() const {
    std::vector<std::vector<Vec2>> profiles;

    struct Seg { Vec2 a, b; bool used = false; };
    std::vector<Seg> segments;

    for (const auto& e : entities) {
        if (e.construction) continue;
        switch (e.type) {
        case SketchEntityType::Line:
            segments.push_back({e.p0, e.p1, false});
            break;
        case SketchEntityType::Rectangle: {
            Vec2 c0 = e.p0, c1 = {e.p1.x, e.p0.y}, c2 = e.p1, c3 = {e.p0.x, e.p1.y};
            segments.push_back({c0, c1, false});
            segments.push_back({c1, c2, false});
            segments.push_back({c2, c3, false});
            segments.push_back({c3, c0, false});
            break;
        }
        case SketchEntityType::Circle: {
            int segs = circleSegmentCount(e.radius);
            std::vector<Vec2> pts;
            for (int i = 0; i < segs; i++) {
                float angle = (float)i / segs * 2.0f * PI;
                pts.push_back(e.p0 + Vec2{std::cos(angle) * e.radius, std::sin(angle) * e.radius});
            }
            profiles.push_back(pts);
            break;
        }
        default: break;
        }
    }

    const float tol = snapDistance * 2.0f;
    auto pointsMatch = [&](Vec2 a, Vec2 b) -> bool { return a.distTo(b) < tol; };

    for (int startIdx = 0; startIdx < (int)segments.size(); startIdx++) {
        if (segments[startIdx].used) continue;
        std::vector<Vec2> chain;
        chain.push_back(segments[startIdx].a);
        chain.push_back(segments[startIdx].b);
        std::vector<bool> localUsed(segments.size(), false);
        localUsed[startIdx] = true;

        bool extended = true;
        while (extended) {
            extended = false;
            Vec2 tip = chain.back();
            if (chain.size() >= 3 && pointsMatch(tip, chain.front())) {
                chain.pop_back();
                for (int i = 0; i < (int)segments.size(); i++)
                    if (localUsed[i]) segments[i].used = true;
                if (chain.size() >= 3) profiles.push_back(chain);
                break;
            }
            for (int i = 0; i < (int)segments.size(); i++) {
                if (localUsed[i]) continue;
                if (pointsMatch(tip, segments[i].a)) {
                    chain.push_back(segments[i].b);
                    localUsed[i] = true;
                    extended = true;
                    break;
                }
                if (pointsMatch(tip, segments[i].b)) {
                    chain.push_back(segments[i].a);
                    localUsed[i] = true;
                    extended = true;
                    break;
                }
            }
        }
    }
    return profiles;
}

bool Sketch::trimAtPoint(Vec2 point, float tolerance) {
    // Find the nearest entity
    int idx = pickEntity(point, tolerance);
    if (idx < 0) return false;

    auto& e = entities[idx];

    switch (e.type) {
    case SketchEntityType::Line: {
        // Find intersections with other lines
        Vec2 a = e.p0, b = e.p1;
        struct Intersection { float t; Vec2 point; };
        std::vector<Intersection> intersections;

        for (int i = 0; i < (int)entities.size(); i++) {
            if (i == idx) continue;
            const auto& other = entities[i];
            if (other.type == SketchEntityType::Line) {
                Vec2 c = other.p0, d = other.p1;
                Vec2 ab = b - a, cd = d - c;
                float denom = ab.cross(cd);
                if (std::abs(denom) < EPSILON) continue;
                float t = (c - a).cross(cd) / denom;
                float u = (c - a).cross(ab) / denom;
                if (t > EPSILON && t < 1.0f - EPSILON && u > EPSILON && u < 1.0f - EPSILON) {
                    intersections.push_back({t, a + ab * t});
                }
            }
        }

        if (intersections.empty()) {
            // No intersections — just delete the whole entity
            entities.erase(entities.begin() + idx);
            return true;
        }

        // Sort intersections by parameter
        std::sort(intersections.begin(), intersections.end(),
            [](const Intersection& a, const Intersection& b) { return a.t < b.t; });

        // Find which segment the click point is on
        Vec2 ab = b - a;
        float clickT = (point - a).dot(ab) / ab.lengthSq();

        // Find bounding intersections around the click
        float tLow = 0.0f, tHigh = 1.0f;
        for (const auto& isect : intersections) {
            if (isect.t < clickT) tLow = isect.t;
            if (isect.t > clickT) { tHigh = isect.t; break; }
        }

        // Remove the clicked entity and add back the remaining segments
        entities.erase(entities.begin() + idx);
        if (tLow > EPSILON) {
            addLine(a, a + ab * tLow);
        }
        if (tHigh < 1.0f - EPSILON) {
            addLine(a + ab * tHigh, b);
        }
        return true;
    }
    case SketchEntityType::Circle:
    case SketchEntityType::Rectangle:
        // Simple: just delete
        entities.erase(entities.begin() + idx);
        return true;
    default:
        entities.erase(entities.begin() + idx);
        return true;
    }
}

void Sketch::offsetProfiles(float distance) {
    auto profiles = extractProfiles2D();
    for (const auto& profile : profiles) {
        auto offsetPts = offsetProfile2D(profile, distance);
        int n = (int)offsetPts.size();
        for (int i = 0; i < n; i++) {
            addLine(offsetPts[i], offsetPts[(i + 1) % n]);
        }
    }
}

} // namespace cad
