#include "geometry.h"
#include <unordered_map>
#include <algorithm>
#include <cmath>

namespace cad {

// ---- Ear-clipping triangulation for concave polygons ----
void triangulatePolygon(const std::vector<Vec3>& polygon, Vec3 normal,
                        std::vector<Vec3>& outPositions, std::vector<Vec3>& outNormals,
                        std::vector<uint32_t>& outIndices, uint32_t baseIndex) {
    int n = (int)polygon.size();
    if (n < 3) return;

    // Project to 2D for ear testing
    int axis = 0;
    if (std::abs(normal.y) > std::abs(normal.x) && std::abs(normal.y) > std::abs(normal.z)) axis = 1;
    else if (std::abs(normal.z) > std::abs(normal.x)) axis = 2;

    auto to2D = [axis](Vec3 p) -> Vec2 {
        switch (axis) {
            case 0: return {p.y, p.z};
            case 1: return {p.x, p.z};
            case 2: return {p.x, p.y};
        }
        return {p.x, p.y};
    };

    // Compute signed area to determine winding
    std::vector<Vec2> pts2d(n);
    for (int i = 0; i < n; i++) pts2d[i] = to2D(polygon[i]);

    float area = 0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        area += pts2d[i].x * pts2d[j].y - pts2d[j].x * pts2d[i].y;
    }
    // If winding matches normal direction, keep; otherwise we may need to flip
    // Positive area = CCW in 2D
    bool flipWinding = area < 0;

    std::vector<int> indices(n);
    for (int i = 0; i < n; i++) indices[i] = i;

    auto isEar = [&](int prev, int cur, int next) -> bool {
        Vec2 a = pts2d[indices[prev]];
        Vec2 b = pts2d[indices[cur]];
        Vec2 c = pts2d[indices[next]];

        // Check convexity
        float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (flipWinding ? cross > -EPSILON : cross < EPSILON) return false;

        // Check no other point is *strictly* inside the triangle.
        //
        // The test has to be strict, and has to ignore vertices coincident with
        // a corner.  Bridging a hole into the outer loop duplicates two
        // vertices and lays two edges on top of each other, so points sit
        // exactly on triangle boundaries by construction.  Counting those as
        // "inside" rejects every candidate ear and the polygon silently
        // triangulates to nothing.
        const float EPS = 1e-7f;
        int count = (int)indices.size();
        for (int i = 0; i < count; i++) {
            if (i == prev || i == cur || i == next) continue;
            Vec2 p = pts2d[indices[i]];
            if ((p - a).lengthSq() < EPS || (p - b).lengthSq() < EPS ||
                (p - c).lengthSq() < EPS) continue;   // duplicate of a corner

            float d1 = (p.x - a.x) * (b.y - a.y) - (b.x - a.x) * (p.y - a.y);
            float d2 = (p.x - b.x) * (c.y - b.y) - (c.x - b.x) * (p.y - b.y);
            float d3 = (p.x - c.x) * (a.y - c.y) - (a.x - c.x) * (p.y - c.y);
            bool allPos = (d1 > EPS) && (d2 > EPS) && (d3 > EPS);
            bool allNeg = (d1 < -EPS) && (d2 < -EPS) && (d3 < -EPS);
            if (allPos || allNeg) return false;   // strictly inside
        }
        return true;
    };

    while ((int)indices.size() > 2) {
        int count = (int)indices.size();
        bool foundEar = false;
        for (int i = 0; i < count; i++) {
            int prev = (i + count - 1) % count;
            int next = (i + 1) % count;
            if (isEar(prev, i, next)) {
                uint32_t base = (uint32_t)outPositions.size() + baseIndex;
                outPositions.push_back(polygon[indices[prev]]);
                outPositions.push_back(polygon[indices[i]]);
                outPositions.push_back(polygon[indices[next]]);
                outNormals.push_back(normal);
                outNormals.push_back(normal);
                outNormals.push_back(normal);
                outIndices.push_back(base);
                outIndices.push_back(base + 1);
                outIndices.push_back(base + 2);
                indices.erase(indices.begin() + i);
                foundEar = true;
                break;
            }
        }
        if (!foundEar) {
            // Ear clipping can stall on polygons carrying several keyhole
            // bridges, where duplicated vertices and coincident edges defeat
            // the containment test.  Abandoning the polygon there leaves a gap
            // in the surface (a five-hole plate lost its cap and leaked), so
            // clip the most convex corner instead and carry on — progress is
            // then guaranteed and the result stays closed.
            int bestI = -1;
            float bestScore = 0.0f;
            for (int i = 0; i < count; i++) {
                int prev = (i + count - 1) % count;
                int next = (i + 1) % count;
                Vec2 a = pts2d[indices[prev]];
                Vec2 b = pts2d[indices[i]];
                Vec2 c = pts2d[indices[next]];
                float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
                float score = flipWinding ? -cross : cross;
                if (bestI < 0 || score > bestScore) { bestI = i; bestScore = score; }
            }
            if (bestI < 0) break;

            int prev = (bestI + count - 1) % count;
            int next = (bestI + 1) % count;

            // A corner with no area contributes nothing but a degenerate
            // triangle whose edges cannot pair up.  Drop the vertex and move on.
            if (std::abs(bestScore) < 1e-9f) {
                indices.erase(indices.begin() + bestI);
                continue;
            }

            uint32_t base = (uint32_t)outPositions.size() + baseIndex;
            outPositions.push_back(polygon[indices[prev]]);
            outPositions.push_back(polygon[indices[bestI]]);
            outPositions.push_back(polygon[indices[next]]);
            outNormals.push_back(normal);
            outNormals.push_back(normal);
            outNormals.push_back(normal);
            outIndices.push_back(base);
            outIndices.push_back(base + 1);
            outIndices.push_back(base + 2);
            indices.erase(indices.begin() + bestI);
        }
    }
}

// ---- Solid methods ----

void Solid::rebuildEdges() {
    edges.clear();

    // A hash, not a positional encoding.  The previous scheme packed three
    // coordinates into one integer in base 1,000,001, which is only injective
    // while every coordinate stays inside +/-50 mm — beyond that the digits
    // overflow, negatives wrap, and unrelated edges could silently be welded
    // together.  Hashing removes the range limit, and because a hash can
    // collide by nature, every hit is now confirmed against the real endpoints.
    auto quantize = [](Vec3 v) -> uint64_t {
        auto qi = [](float f) -> uint64_t {
            return (uint64_t)(int64_t)std::llround((double)f * 10000.0);
        };
        uint64_t h = 1469598103934665603ULL;
        for (uint64_t part : { qi(v.x), qi(v.y), qi(v.z) }) {
            h ^= part;
            h *= 1099511628211ULL;
        }
        return h;
    };

    auto edgeHash = [&](Vec3 a, Vec3 b) -> uint64_t {
        uint64_t ha = quantize(a), hb = quantize(b);
        uint64_t lo = ha < hb ? ha : hb, hi = ha < hb ? hb : ha;
        uint64_t h = lo ^ (hi + 0x9e3779b97f4a7c15ULL + (lo << 6) + (lo >> 2));
        return h;
    };

    const float weldTol = 1e-4f;
    auto sameEdge = [&](const BRepEdge& e, Vec3 a, Vec3 b) {
        return (e.start.distTo(a) < weldTol && e.end.distTo(b) < weldTol) ||
               (e.start.distTo(b) < weldTol && e.end.distTo(a) < weldTol);
    };

    std::unordered_map<uint64_t, std::vector<uint32_t>> edgeMap;

    for (uint32_t fi = 0; fi < (uint32_t)faces.size(); fi++) {
        // Hole boundaries are real edges of the face — the rim of a bore has to
        // appear in the edge list or it can never be selected or drawn.
        auto addLoop = [&](const std::vector<Vec3>& loop) {
            for (size_t i = 0; i < loop.size(); i++) {
                Vec3 a = loop[i];
                Vec3 b = loop[(i + 1) % loop.size()];
                uint64_t key = edgeHash(a, b);
                auto& bucket = edgeMap[key];

                // Confirm against the actual endpoints; a bucket may hold more
                // than one edge if two different edges happen to hash alike.
                uint32_t found = NULL_ID;
                for (uint32_t cand : bucket) {
                    if (sameEdge(edges[cand], a, b)) { found = cand; break; }
                }
                if (found == NULL_ID) {
                    uint32_t ei = (uint32_t)edges.size();
                    edges.push_back({a, b, fi, NULL_ID});
                    bucket.push_back(ei);
                } else {
                    edges[found].face1 = fi;
                }
            }
        };
        addLoop(faces[fi].outerLoop);
        for (const auto& hole : faces[fi].innerLoops) addLoop(hole);
    }
}

std::vector<int> Solid::smoothSurfaces() const {
    std::vector<int> label(faces.size(), -1);
    int next = 0;
    for (uint32_t seed = 0; seed < (uint32_t)faces.size(); seed++) {
        if (label[seed] != -1) continue;
        int id = next++;
        std::vector<uint32_t> stack{ seed };
        label[seed] = id;
        while (!stack.empty()) {
            uint32_t fi = stack.back();
            stack.pop_back();
            for (const auto& e : edges) {
                if (e.face0 == NULL_ID || e.face1 == NULL_ID) continue;
                if (e.face0 >= faces.size() || e.face1 >= faces.size()) continue;
                uint32_t other = (e.face0 == fi) ? e.face1 : (e.face1 == fi ? e.face0 : NULL_ID);
                if (other == NULL_ID || label[other] != -1) continue;
                // Same crease angle the shading and wireframe already use.
                if (faces[fi].normal.dot(faces[other].normal) <= 0.819f) continue;
                label[other] = id;
                stack.push_back(other);
            }
        }
    }
    return label;
}

std::vector<uint32_t> Solid::curveGroupEdges(uint32_t ei) const {
    if (ei >= edges.size()) return {};
    const BRepEdge& e = edges[ei];
    if (e.face0 == NULL_ID || e.face1 == NULL_ID) return { ei };
    if (e.face0 >= faces.size() || e.face1 >= faces.size()) return { ei };

    // A logical curved edge is the whole boundary between two smooth surfaces.
    // The rim of a bore separates the flat cap from the tessellated wall, so
    // every one of its segments shares the same surface pair — which is what
    // lets one click select the entire circle instead of a single facet.
    // The old rule only recognised faces tagged by fillet(), so extruded
    // circles were never grouped at all.
    std::vector<int> surf = smoothSurfaces();
    int a = surf[e.face0], b = surf[e.face1];
    if (a == b) return { ei };            // interior seam of one smooth surface

    std::vector<uint32_t> out;
    for (uint32_t i = 0; i < (uint32_t)edges.size(); i++) {
        const auto& x = edges[i];
        if (x.face0 == NULL_ID || x.face1 == NULL_ID) continue;
        if (x.face0 >= faces.size() || x.face1 >= faces.size()) continue;
        int xa = surf[x.face0], xb = surf[x.face1];
        if ((xa == a && xb == b) || (xa == b && xb == a)) out.push_back(i);
    }
    if (out.empty()) out.push_back(ei);
    return out;
}

// Average normals between triangles that meet at a shallow angle, leaving
// genuinely sharp edges alone.  Curved surfaces in this kernel are polygonal
// approximations — a cylinder is a many-sided prism — so without this every
// circle reads as a visible band of facets.  A box keeps crisp 90° corners
// because those exceed the crease threshold.
static void smoothMeshNormals(Mesh& mesh, float creaseAngleDeg) {
    const size_t n = mesh.positions.size();
    if (n == 0 || mesh.normals.size() != n) return;

    const float cosCrease = std::cos(creaseAngleDeg * DEG2RAD);

    // Bucket vertices by position.  Welding at ~0.5 µm is far below any
    // tolerance the modeller works at, so coincident corners always land in
    // the same bucket.
    auto posKey = [](const Vec3& v) -> uint64_t {
        auto q = [](float f) -> uint64_t {
            return (uint64_t)(int64_t)std::llround((double)f * 2048.0);
        };
        uint64_t h = 1469598103934665603ULL;
        for (uint64_t part : { q(v.x), q(v.y), q(v.z) }) {
            h ^= part;
            h *= 1099511628211ULL;
        }
        return h;
    };

    std::unordered_map<uint64_t, std::vector<uint32_t>> buckets;
    buckets.reserve(n * 2);
    for (uint32_t i = 0; i < (uint32_t)n; i++)
        buckets[posKey(mesh.positions[i])].push_back(i);

    std::vector<Vec3> smoothed = mesh.normals;
    for (const auto& entry : buckets) {
        const std::vector<uint32_t>& group = entry.second;
        if (group.size() < 2) continue;
        for (uint32_t i : group) {
            const Vec3 ni = mesh.normals[i];
            Vec3 acc = ni;
            for (uint32_t j : group) {
                if (j == i) continue;
                if (ni.dot(mesh.normals[j]) >= cosCrease) acc += mesh.normals[j];
            }
            Vec3 result = acc.normalized();
            if (result.lengthSq() > 0.25f) smoothed[i] = result;
        }
    }
    mesh.normals.swap(smoothed);
}

// Cut each hole into the outer loop with a "keyhole" bridge: a pair of
// coincident edges joining a hole vertex to a mutually visible outer vertex.
// The result is one simple (self-touching but not self-crossing) polygon, which
// ordinary ear clipping handles correctly.
std::vector<Vec3> bridgeHoles(const std::vector<Vec3>& outer,
                              const std::vector<std::vector<Vec3>>& holes,
                              Vec3 normal) {
    if (holes.empty() || outer.size() < 3) return outer;

    // Work in 2D by dropping the axis most aligned with the face normal.
    int axis = 0;
    if (std::abs(normal.y) > std::abs(normal.x) && std::abs(normal.y) > std::abs(normal.z)) axis = 1;
    else if (std::abs(normal.z) > std::abs(normal.x)) axis = 2;
    auto to2D = [axis](const Vec3& p) -> Vec2 {
        switch (axis) { case 0: return {p.y, p.z}; case 1: return {p.x, p.z}; default: return {p.x, p.y}; }
    };

    // Orientation is judged against the face normal, never against a 2D
    // projection.  Both caps of an extrusion project onto the same plane, so
    // normalising to "CCW in 2D" would give them identical winding and leave
    // one of them facing the wrong way — a watertight but inside-out solid.
    auto facing = [&](const std::vector<Vec3>& loop) {
        Vec3 n{0, 0, 0};
        for (size_t i = 0; i < loop.size(); i++)
            n += loop[i].cross(loop[(i + 1) % loop.size()]);
        return n.dot(normal);
    };

    // Segments cross only if each straddles the other's supporting line.
    // Endpoint contact is allowed — bridges legitimately touch at their ends.
    auto properlyCrosses = [&](Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
        auto side = [](Vec2 p, Vec2 q, Vec2 r) {
            float v = (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
            return (v > 1e-9f) ? 1 : (v < -1e-9f ? -1 : 0);
        };
        int d1 = side(a, b, c), d2 = side(a, b, d);
        int d3 = side(c, d, a), d4 = side(c, d, b);
        return d1 * d2 < 0 && d3 * d4 < 0;
    };

    std::vector<Vec3> result = outer;
    if (facing(result) < 0) std::reverse(result.begin(), result.end());

    // Merge holes one at a time; each merge grows the polygon that later holes
    // must be tested against.
    std::vector<std::vector<Vec3>> pending;
    for (const auto& h : holes) {
        if (h.size() < 3) continue;
        std::vector<Vec3> hole = h;
        if (facing(hole) > 0) std::reverse(hole.begin(), hole.end());  // opposite to outer
        pending.push_back(std::move(hole));
    }

    // Merge the holes right-to-left by their extreme x in the projection.  This
    // is the classic ordering for keyhole bridging: taking the rightmost hole
    // first means each bridge runs into already-merged territory rather than
    // across a hole still waiting its turn, which keeps later bridges from
    // being boxed in and falling back to a degenerate cut.
    std::sort(pending.begin(), pending.end(),
              [&](const std::vector<Vec3>& a, const std::vector<Vec3>& b) {
                  float ax = -1e30f, bx = -1e30f;
                  for (const Vec3& p : a) ax = std::max(ax, to2D(p).x);
                  for (const Vec3& p : b) bx = std::max(bx, to2D(p).x);
                  return ax > bx;
              });

    for (size_t hi = 0; hi < pending.size(); hi++) {
        const std::vector<Vec3>& hole = pending[hi];

        // Shortest bridge that crosses nothing: the outer boundary so far, this
        // hole, or any hole not yet merged.
        float bestLen = 1e30f;
        size_t bestOuter = SIZE_MAX, bestHole = SIZE_MAX;

        for (size_t oi = 0; oi < result.size(); oi++) {
            for (size_t hj = 0; hj < hole.size(); hj++) {
                Vec2 A = to2D(result[oi]), B = to2D(hole[hj]);
                float len = (A - B).lengthSq();
                if (len >= bestLen) continue;

                bool blocked = false;
                for (size_t k = 0; k < result.size() && !blocked; k++) {
                    if (k == oi || (k + 1) % result.size() == oi) continue;
                    if (properlyCrosses(A, B, to2D(result[k]), to2D(result[(k + 1) % result.size()])))
                        blocked = true;
                }
                for (size_t k = 0; k < hole.size() && !blocked; k++) {
                    if (k == hj || (k + 1) % hole.size() == hj) continue;
                    if (properlyCrosses(A, B, to2D(hole[k]), to2D(hole[(k + 1) % hole.size()])))
                        blocked = true;
                }
                for (size_t o = hi + 1; o < pending.size() && !blocked; o++) {
                    const auto& other = pending[o];
                    for (size_t k = 0; k < other.size() && !blocked; k++) {
                        if (properlyCrosses(A, B, to2D(other[k]), to2D(other[(k + 1) % other.size()])))
                            blocked = true;
                    }
                }
                if (blocked) continue;

                bestLen = len;
                bestOuter = oi;
                bestHole = hj;
            }
        }
        if (bestOuter == SIZE_MAX) continue;   // no visible bridge; skip this hole

        // Splice: ...outer[bestOuter], hole[bestHole..wrap..bestHole], outer[bestOuter]...
        std::vector<Vec3> spliced;
        spliced.reserve(result.size() + hole.size() + 2);
        for (size_t i = 0; i <= bestOuter; i++) spliced.push_back(result[i]);
        for (size_t k = 0; k <= hole.size(); k++)
            spliced.push_back(hole[(bestHole + k) % hole.size()]);
        spliced.push_back(result[bestOuter]);
        for (size_t i = bestOuter + 1; i < result.size(); i++) spliced.push_back(result[i]);
        result.swap(spliced);
    }
    return result;
}

Mesh Solid::tessellate() const {
    Mesh mesh;
    for (const auto& face : faces) {
        if (face.outerLoop.size() < 3) continue;
        // Bridge any holes into the outer loop first, so a face with a bore
        // triangulates as an annulus rather than a filled disc.
        const std::vector<Vec3> loop = face.innerLoops.empty()
            ? face.outerLoop
            : bridgeHoles(face.outerLoop, face.innerLoops, face.normal);
        triangulatePolygon(loop, face.normal,
                          mesh.positions, mesh.normals, mesh.indices, 0);
    }
    // Fix indices: triangulatePolygon builds absolute indices with baseIndex=0
    // but positions grow, so we use them as-is since each call to triangulatePolygon
    // appends and uses absolute positions offset

    // 35° keeps box corners sharp while smoothing circles down to ~10 segments.
    smoothMeshNormals(mesh, 35.0f);
    return mesh;
}

std::vector<uint32_t> Solid::coplanarRegion(uint32_t faceIdx) const {
    if (faceIdx >= faces.size()) return {};
    if (faces[faceIdx].outerLoop.size() < 3) return { faceIdx };

    // Build face adjacency from shared edges.  This deliberately does not use
    // the stored BRepEdge list: that caps adjacency at two faces per edge and
    // silently drops the rest, which is precisely the data loss that makes
    // fragmented faces unselectable.
    auto vertexKey = [](const Vec3& v) -> uint64_t {
        auto q = [](float f) -> int64_t {
            return (int64_t)std::llround((double)f * 10000.0);
        };
        uint64_t h = 1469598103934665603ULL;
        for (int64_t part : { q(v.x), q(v.y), q(v.z) }) {
            h ^= (uint64_t)part;
            h *= 1099511628211ULL;
        }
        return h;
    };

    // Adjacency is keyed on shared *vertices*, not shared edges.  BSP splitting
    // leaves T-junctions — one fragment spans P0→P2 while its neighbours span
    // P0→P1 and P1→P2 — so coincident fragments frequently share no identical
    // edge at all.  They do still share corner points.
    std::unordered_map<uint64_t, std::vector<uint32_t>> vertexToFaces;
    vertexToFaces.reserve(faces.size() * 4);
    for (uint32_t fi = 0; fi < (uint32_t)faces.size(); fi++) {
        for (const Vec3& p : faces[fi].outerLoop)
            vertexToFaces[vertexKey(p)].push_back(fi);
        for (const auto& hole : faces[fi].innerLoops)
            for (const Vec3& p : hole)
                vertexToFaces[vertexKey(p)].push_back(fi);
    }

    const Vec3 seedNormal = faces[faceIdx].normal;
    const Vec3 seedPoint  = faces[faceIdx].outerLoop[0];

    auto sameSurface = [&](uint32_t fi) {
        const auto& f = faces[fi];
        if (f.outerLoop.empty()) return false;
        if (f.normal.dot(seedNormal) < 0.9995f) return false;          // parallel
        return std::abs(seedNormal.dot(f.outerLoop[0] - seedPoint)) < 1e-4f;  // same plane
    };

    std::vector<uint32_t> region;
    std::vector<bool> seen(faces.size(), false);
    std::vector<uint32_t> stack{ faceIdx };
    seen[faceIdx] = true;

    while (!stack.empty()) {
        uint32_t fi = stack.back();
        stack.pop_back();
        region.push_back(fi);

        auto visitLoop = [&](const std::vector<Vec3>& loop) {
            for (const Vec3& p : loop) {
                auto it = vertexToFaces.find(vertexKey(p));
                if (it == vertexToFaces.end()) continue;
                for (uint32_t nb : it->second) {
                    if (seen[nb] || !sameSurface(nb)) continue;
                    seen[nb] = true;
                    stack.push_back(nb);
                }
            }
        };
        visitLoop(faces[fi].outerLoop);
        for (const auto& hole : faces[fi].innerLoops) visitLoop(hole);
    }
    return region;
}

std::vector<std::vector<Vec3>> Solid::regionBoundaryLoops(
        const std::vector<uint32_t>& region) const {
    if (region.empty()) return {};

    auto key = [](const Vec3& v) -> uint64_t {
        auto q = [](float f) -> int64_t { return (int64_t)std::llround((double)f * 10000.0); };
        uint64_t h = 1469598103934665603ULL;
        for (int64_t part : { q(v.x), q(v.y), q(v.z) }) {
            h ^= (uint64_t)part; h *= 1099511628211ULL;
        }
        return h;
    };

    // Every distinct corner in the region.  Needed because BSP fragments meet
    // at T-junctions: one fragment's edge runs straight past a vertex that its
    // neighbour treats as a corner, so seams only cancel after being split at
    // those intervening points.
    std::vector<Vec3> points;
    std::unordered_map<uint64_t, uint32_t> pointIds;
    auto idOf = [&](const Vec3& v) -> uint32_t {
        uint64_t k = key(v);
        auto it = pointIds.find(k);
        if (it != pointIds.end()) return it->second;
        uint32_t id = (uint32_t)points.size();
        points.push_back(v);
        pointIds[k] = id;
        return id;
    };
    for (uint32_t fi : region) {
        if (fi >= faces.size()) return {};
        for (const Vec3& p : faces[fi].outerLoop) idOf(p);
        // Hole rims bound the region just as much as its outer edge.  Omitting
        // them silently returns a solid sheet, so re-extruding a face that
        // already has a bore fills the bore back in.
        for (const auto& hole : faces[fi].innerLoops)
            for (const Vec3& p : hole) idOf(p);
    }

    // Directed edges, each split at any region corner lying on its interior.
    struct DEdge { uint32_t a, b; };
    std::vector<DEdge> directed;
    auto addLoopEdges = [&](const std::vector<Vec3>& loop) {
        for (size_t i = 0; i < loop.size(); i++) {
            Vec3 A = loop[i];
            Vec3 B = loop[(i + 1) % loop.size()];
            Vec3 ab = B - A;
            float len2 = ab.lengthSq();
            if (len2 < 1e-12f) continue;

            std::vector<std::pair<float, uint32_t>> cuts;
            for (uint32_t pid = 0; pid < (uint32_t)points.size(); pid++) {
                const Vec3& P = points[pid];
                float t = (P - A).dot(ab) / len2;
                if (t <= 1e-5f || t >= 1.0f - 1e-5f) continue;
                if ((A + ab * t).distTo(P) > 1e-4f) continue;   // not on the segment
                cuts.push_back({t, pid});
            }
            std::sort(cuts.begin(), cuts.end(),
                      [](const auto& x, const auto& y) { return x.first < y.first; });

            uint32_t prev = idOf(A);
            for (auto& c : cuts) { directed.push_back({prev, c.second}); prev = c.second; }
            directed.push_back({prev, idOf(B)});
        }
    };
    for (uint32_t fi : region) {
        addLoopEdges(faces[fi].outerLoop);
        for (const auto& hole : faces[fi].innerLoops) addLoopEdges(hole);
    }

    // A seam interior to the region is traversed once in each direction and
    // cancels; what survives exactly once is boundary.
    std::unordered_map<uint64_t, int> useCount;
    auto undirected = [](uint32_t a, uint32_t b) -> uint64_t {
        return (a < b) ? ((uint64_t)a << 32 | b) : ((uint64_t)b << 32 | a);
    };
    for (const auto& e : directed) useCount[undirected(e.a, e.b)]++;

    std::unordered_map<uint32_t, std::vector<uint32_t>> next;
    size_t boundaryCount = 0;
    for (const auto& e : directed) {
        if (useCount[undirected(e.a, e.b)] != 1) continue;
        next[e.a].push_back(e.b);
        boundaryCount++;
    }
    if (boundaryCount < 3) return {};

    // Chain the surviving edges into closed loops.
    std::vector<std::vector<Vec3>> loops;
    std::unordered_map<uint64_t, bool> used;
    for (const auto& entry : next) {
        for (uint32_t startB : entry.second) {
            uint32_t startA = entry.first;
            if (used[undirected(startA, startB)]) continue;

            std::vector<Vec3> loop;
            uint32_t a = startA, b = startB;
            for (size_t guard = 0; guard <= boundaryCount; guard++) {
                used[undirected(a, b)] = true;
                loop.push_back(points[a]);
                if (b == startA) break;
                auto it = next.find(b);
                if (it == next.end()) { loop.clear(); break; }
                uint32_t nb = UINT32_MAX;
                for (uint32_t cand : it->second) {
                    if (!used[undirected(b, cand)]) { nb = cand; break; }
                }
                if (nb == UINT32_MAX) { loop.clear(); break; }
                a = b; b = nb;
            }
            if (loop.size() >= 3) loops.push_back(std::move(loop));
        }
    }
    if (loops.empty()) return {};

    // Largest loop is the outer boundary; the rest are holes.
    auto loopArea = [](const std::vector<Vec3>& L) {
        Vec3 n{0, 0, 0};
        for (size_t i = 0; i < L.size(); i++) n += L[i].cross(L[(i + 1) % L.size()]);
        return n.length() * 0.5f;
    };
    size_t outer = 0;
    for (size_t i = 1; i < loops.size(); i++)
        if (loopArea(loops[i]) > loopArea(loops[outer])) outer = i;
    if (outer != 0) std::swap(loops[0], loops[outer]);

    // Drop vertices that sit on the straight line between their neighbours.
    // Splitting edges at T-junctions leaves a lot of them, and keeping them
    // turns one straight edge into a run of short collinear ones — identical
    // on screen, but it inflates the edge list and makes edge picking grab a
    // fragment instead of the whole edge.
    for (auto& loop : loops) {
        if (loop.size() < 4) continue;
        std::vector<Vec3> simplified;
        simplified.reserve(loop.size());
        for (size_t i = 0; i < loop.size(); i++) {
            const Vec3& prev = loop[(i + loop.size() - 1) % loop.size()];
            const Vec3& cur  = loop[i];
            const Vec3& next = loop[(i + 1) % loop.size()];
            Vec3 a = cur - prev, b = next - cur;
            float la = a.length(), lb = b.length();
            if (la < 1e-6f) continue;                      // duplicate point
            if (lb > 1e-6f && a.cross(b).length() / (la * lb) < 1e-4f) continue;  // collinear
            simplified.push_back(cur);
        }
        if (simplified.size() >= 3) loop.swap(simplified);
    }
    return loops;
}

Wire Solid::wireframe() const {
    Wire w;
    for (const auto& e : edges) {
        // A closed solid has exactly two faces on every edge.  An edge that
        // never got a second one is an artefact of boolean fragmentation and
        // the two-face adjacency limit (REVIEW.md 2.2 / 2.3), not a feature
        // line — drawing those covers the model in stray scratches.
        if (e.face0 == NULL_ID || e.face1 == NULL_ID) continue;
        if (e.face0 >= faces.size() || e.face1 >= faces.size()) continue;

        const auto& f0 = faces[e.face0];
        const auto& f1 = faces[e.face1];
        if (f0.outerLoop.empty() || f1.outerLoop.empty()) continue;

        // Skip any seam shallow enough that the shading already treats it as
        // smooth (same 35° crease angle used for vertex normals).  Without
        // this a tessellated cylinder draws every facet line and the
        // wireframe reads as noise rather than feature edges.
        if (f0.normal.dot(f1.normal) > 0.819f) continue;   // cos(35°)

        w.points.push_back(e.start);
        w.points.push_back(e.end);
    }
    return w;
}

AABB Solid::bounds() const {
    AABB bb;
    for (const auto& face : faces)
        for (const auto& p : face.outerLoop)
            bb.expand(p);
    return bb;
}

// ---- Extrude ----
// Reorder a closed loop so its Newell normal points along `dir`.  Extrusion
// winds its caps and side walls from the profile, so a profile that happens to
// arrive clockwise produces a solid whose every normal points inward — which
// then aims the push/pull arrow into the body instead of out of it.
static void orientLoop(std::vector<Vec3>& loop, Vec3 dir) {
    if (loop.size() < 3) return;
    Vec3 n{0, 0, 0};
    for (size_t i = 0; i < loop.size(); i++)
        n += loop[i].cross(loop[(i + 1) % loop.size()]);
    if (n.dot(dir) < 0.0f) std::reverse(loop.begin(), loop.end());
}

Solid extrude(const std::vector<Vec3>& rawProfile, Vec3 direction, float distance) {
    Solid solid;
    if (rawProfile.size() < 3) return solid;

    std::vector<Vec3> profile = rawProfile;
    orientLoop(profile, direction);

    Vec3 offset = direction.normalized() * distance;
    size_t n = profile.size();

    // Bottom face (original profile, reversed winding for outward normal)
    BRepFace bottom;
    for (int i = (int)n - 1; i >= 0; i--)
        bottom.outerLoop.push_back(profile[i]);
    bottom.computeNormal();
    solid.faces.push_back(bottom);

    // Top face (offset profile)
    BRepFace top;
    for (size_t i = 0; i < n; i++)
        top.outerLoop.push_back(profile[i] + offset);
    top.computeNormal();
    solid.faces.push_back(top);

    // Side faces
    for (size_t i = 0; i < n; i++) {
        size_t j = (i + 1) % n;
        BRepFace side;
        side.outerLoop.push_back(profile[i]);
        side.outerLoop.push_back(profile[j]);
        side.outerLoop.push_back(profile[j] + offset);
        side.outerLoop.push_back(profile[i] + offset);
        side.computeNormal();
        solid.faces.push_back(side);
    }

    solid.rebuildEdges();
    return solid;
}

// ---- Point in polygon (2D ray casting) ----
bool pointInPolygon2D(Vec2 point, const std::vector<Vec2>& polygon) {
    int n = (int)polygon.size();
    if (n < 3) return false;
    int crossings = 0;
    for (int i = 0; i < n; i++) {
        Vec2 a = polygon[i];
        Vec2 b = polygon[(i + 1) % n];
        if ((a.y <= point.y && b.y > point.y) || (b.y <= point.y && a.y > point.y)) {
            float t = (point.y - a.y) / (b.y - a.y);
            if (point.x < a.x + t * (b.x - a.x))
                crossings++;
        }
    }
    return (crossings & 1) != 0;
}

// ---- Extrude with holes ----
Solid extrudeWithHoles(const std::vector<Vec3>& rawOuter,
                       const std::vector<std::vector<Vec3>>& rawHoles,
                       Vec3 direction, float distance) {
    Solid solid;
    if (rawOuter.size() < 3) return solid;

    // Normalise every loop to the same orientation about the extrude direction,
    // so caps and walls come out facing outward whichever way the caller wound
    // its profile.  Hole walls are reversed relative to the outer wall further
    // down, which is what turns them to face into the bore.
    std::vector<Vec3> outerProfile = rawOuter;
    orientLoop(outerProfile, direction);

    std::vector<std::vector<Vec3>> holes;
    holes.reserve(rawHoles.size());
    for (const auto& h : rawHoles) {
        if (h.size() < 3) continue;
        std::vector<Vec3> hole = h;
        orientLoop(hole, direction);
        holes.push_back(std::move(hole));
    }

    Vec3 offset = direction.normalized() * distance;

    // Caps carry their holes as inner loops, so the bore is genuinely absent
    // from the surface rather than covered by a second coplanar polygon.

    // Bottom cap (original side) — outer loop reversed for an outward normal.
    {
        BRepFace bottom;
        for (int i = (int)outerProfile.size() - 1; i >= 0; i--)
            bottom.outerLoop.push_back(outerProfile[i]);
        for (const auto& hole : holes) {
            if (hole.size() < 3) continue;
            std::vector<Vec3> inner;
            for (int i = (int)hole.size() - 1; i >= 0; i--) inner.push_back(hole[i]);
            bottom.innerLoops.push_back(std::move(inner));
        }
        bottom.computeNormal();
        solid.faces.push_back(std::move(bottom));
    }

    // Top cap (offset side)
    {
        BRepFace top;
        for (size_t i = 0; i < outerProfile.size(); i++)
            top.outerLoop.push_back(outerProfile[i] + offset);
        for (const auto& hole : holes) {
            if (hole.size() < 3) continue;
            std::vector<Vec3> inner;
            for (size_t i = 0; i < hole.size(); i++) inner.push_back(hole[i] + offset);
            top.innerLoops.push_back(std::move(inner));
        }
        top.computeNormal();
        solid.faces.push_back(std::move(top));
    }

    // Outer side walls
    size_t n = outerProfile.size();
    for (size_t i = 0; i < n; i++) {
        size_t j = (i + 1) % n;
        BRepFace side;
        side.outerLoop.push_back(outerProfile[i]);
        side.outerLoop.push_back(outerProfile[j]);
        side.outerLoop.push_back(outerProfile[j] + offset);
        side.outerLoop.push_back(outerProfile[i] + offset);
        side.computeNormal();
        solid.faces.push_back(side);
    }

    // Inner hole walls (wound in opposite direction = inward-facing)
    for (auto& hole : holes) {
        size_t hn = hole.size();
        for (size_t i = 0; i < hn; i++) {
            size_t j = (i + 1) % hn;
            BRepFace side;
            // Reversed winding compared to outer walls
            side.outerLoop.push_back(hole[j]);
            side.outerLoop.push_back(hole[i]);
            side.outerLoop.push_back(hole[i] + offset);
            side.outerLoop.push_back(hole[j] + offset);
            side.computeNormal();
            solid.faces.push_back(side);
        }
    }

    solid.rebuildEdges();
    return solid;
}

static bool sameEdgeUnordered(Vec3 a0, Vec3 b0, Vec3 a1, Vec3 b1, float tol = 1e-4f) {
    return (a0.distTo(a1) < tol && b0.distTo(b1) < tol) ||
           (a0.distTo(b1) < tol && b0.distTo(a1) < tol);
}

static bool clipFaceByOffsetFromEdge(const BRepFace& face, Vec3 edgeStart, Vec3 edgeEnd,
                                     float distance, std::vector<Vec3>& outLoop, Vec3& outInward) {
    outLoop.clear();
    if (face.outerLoop.size() < 3) return false;

    Vec3 edgeDir = (edgeEnd - edgeStart).normalized();
    if (edgeDir.lengthSq() < EPSILON) return false;

    // Half-plane clipping is only meaningful when the edge lies on the face's
    // outer boundary, where cutting back from it trims a strip off the rim.
    // For an edge belonging to a hole (or otherwise interior to the face) the
    // same operation slices the entire face in two — chamfering a bore rim used
    // to delete most of the surrounding cap and leave the body full of holes.
    {
        const auto& loop = face.outerLoop;
        bool onOuter = false;
        for (size_t i = 0; i < loop.size() && !onOuter; i++) {
            const Vec3& a = loop[i];
            const Vec3& b = loop[(i + 1) % loop.size()];
            if ((a.distTo(edgeStart) < 1e-4f && b.distTo(edgeEnd) < 1e-4f) ||
                (a.distTo(edgeEnd) < 1e-4f && b.distTo(edgeStart) < 1e-4f)) {
                onOuter = true;
            }
        }
        if (!onOuter) return false;
    }

    Vec3 mid = (edgeStart + edgeEnd) * 0.5f;
    Vec3 inward = face.normal.cross(edgeDir).normalized();
    if ((face.centroid() - mid).dot(inward) < 0.0f) inward = -inward;
    outInward = inward;

    auto signedDist = [&](Vec3 p) {
        return (p - edgeStart).dot(inward) - distance;
    };

    const auto& in = face.outerLoop;
    std::vector<Vec3> clipped;
    for (size_t i = 0; i < in.size(); i++) {
        Vec3 a = in[i];
        Vec3 b = in[(i + 1) % in.size()];
        float da = signedDist(a);
        float db = signedDist(b);
        bool aInside = da >= -1e-5f;
        bool bInside = db >= -1e-5f;

        if (aInside && bInside) {
            clipped.push_back(b);
        } else if (aInside && !bInside) {
            float denom = da - db;
            if (std::abs(denom) > 1e-8f) {
                float t = da / denom;
                clipped.push_back(lerp(a, b, t));
            }
        } else if (!aInside && bInside) {
            float denom = da - db;
            if (std::abs(denom) > 1e-8f) {
                float t = da / denom;
                clipped.push_back(lerp(a, b, t));
            }
            clipped.push_back(b);
        }
    }

    if (clipped.size() < 3) return false;
    outLoop = std::move(clipped);
    return true;
}

// Locate an edge that was recorded before the solid was modified.
//
// Exact endpoint matching is not enough when several edges are processed in one
// go: chamfering an edge trims the corners it shares with its neighbours, so
// those neighbours survive as *shorter* segments lying on the same line, and an
// exact lookup misses every one of them.  That is why chamfering a whole
// circular rim only ever bevelled a single facet.  Fall back to finding the
// remaining piece of the same line.
static int findEdgeIndexByEndpoints(const Solid& s, Vec3 a, Vec3 b) {
    for (int i = 0; i < (int)s.edges.size(); i++) {
        if (sameEdgeUnordered(s.edges[i].start, s.edges[i].end, a, b)) return i;
    }

    Vec3 ab = b - a;
    float abLen = ab.length();
    if (abLen < EPSILON) return -1;
    Vec3 dir = ab / abLen;

    int best = -1;
    float bestLen = 0.0f;
    for (int i = 0; i < (int)s.edges.size(); i++) {
        const BRepEdge& e = s.edges[i];
        Vec3 ed = e.end - e.start;
        float edLen = ed.length();
        if (edLen < EPSILON) continue;

        // Same supporting line: parallel, and both endpoints on it.
        if (std::abs(ed.dot(dir) / edLen) < 0.9999f) continue;
        auto offLine = [&](Vec3 p) {
            Vec3 d = p - a;
            return (d - dir * d.dot(dir)).length();
        };
        if (offLine(e.start) > 1e-4f || offLine(e.end) > 1e-4f) continue;

        // And overlapping the original span, not merely collinear with it.
        float t0 = (e.start - a).dot(dir), t1 = (e.end - a).dot(dir);
        if (t0 > t1) std::swap(t0, t1);
        float overlap = std::min(t1, abLen) - std::max(t0, 0.0f);
        if (overlap <= 1e-4f) continue;

        if (overlap > bestLen) { bestLen = overlap; best = i; }
    }
    return best;
}

// At the corner where the chamfer/fillet meets a perpendicular side face, replace
// the corner vertex with the supplied list of inset points.  `trimF0` and `trimF1`
// must be the first and last entries of `insetPoints` and lie on f0's and f1's
// planes respectively.  The points are inserted in the order that matches the
// side face's existing winding direction.
static void spliceCornerOnSideFaces(Solid& result, FaceId skip0, FaceId skip1,
                                     Vec3 corner,
                                     const std::vector<Vec3>& insetPoints,
                                     const BRepFace& f0, const BRepFace& f1) {
    if (insetPoints.size() < 2) return;
    Vec3 trimF0 = insetPoints.front();
    Vec3 trimF1 = insetPoints.back();
    Plane p0 = f0.plane();
    Plane p1 = f1.plane();

    for (size_t fi = 0; fi < result.faces.size(); fi++) {
        if (fi == skip0 || fi == skip1) continue;
        BRepFace& face = result.faces[fi];
        int k = -1;
        for (size_t i = 0; i < face.outerLoop.size(); i++) {
            if (face.outerLoop[i].distTo(corner) < 1e-4f) { k = (int)i; break; }
        }
        if (k < 0) continue;

        int n = (int)face.outerLoop.size();
        Vec3 prev = face.outerLoop[(k + n - 1) % n];
        Vec3 next = face.outerLoop[(k + 1) % n];

        // Determine which adjacent edge of the side face runs along f0's plane.
        // Walking prev -> corner -> next, if prev lies on f0's plane the f0-side
        // inset must come first, otherwise the f1-side comes first.
        float prevToF0 = std::abs(p0.distTo(prev));
        float nextToF0 = std::abs(p0.distTo(next));
        float prevToF1 = std::abs(p1.distTo(prev));
        float nextToF1 = std::abs(p1.distTo(next));
        bool prevOnF0 = (prevToF0 + nextToF1) < (nextToF0 + prevToF1);

        std::vector<Vec3> newLoop;
        newLoop.reserve(n + insetPoints.size() - 1);
        for (int i = 0; i < n; i++) {
            if (i == k) {
                if (prevOnF0) {
                    for (const auto& p : insetPoints) newLoop.push_back(p);
                } else {
                    for (auto it = insetPoints.rbegin(); it != insetPoints.rend(); ++it)
                        newLoop.push_back(*it);
                }
            } else {
                newLoop.push_back(face.outerLoop[i]);
            }
        }
        face.outerLoop = std::move(newLoop);
        (void)trimF0; (void)trimF1; // documented intent; unused locally
    }
}

// ---- Chamfer (trim adjacent faces and replace edge with bevel face) ----
// Close any gap left behind by an operation, by chaining the edges that ended
// up with only one adjacent face into loops and capping each one.
//
// Chamfering or filleting several edges that meet at a corner leaves a small
// hole there — each edge trims its own two faces, and nothing fills the wedge
// between them.  On a cube with every edge chamfered that is eight triangular
// holes, which is enough to make the STL unprintable.
// Drop repeated vertices from every face loop.  Splicing arc or bevel points
// into the neighbouring faces can insert a point that is already there, which
// leaves a zero-length edge; those read as unpaired edges and make an otherwise
// sound solid look non-manifold.
static void removeDegenerateVertices(Solid& solid) {
    for (auto& face : solid.faces) {
        auto clean = [](std::vector<Vec3>& loop) {
            if (loop.size() < 3) return;
            std::vector<Vec3> out;
            out.reserve(loop.size());
            for (size_t i = 0; i < loop.size(); i++) {
                const Vec3& cur = loop[i];
                const Vec3& next = loop[(i + 1) % loop.size()];
                if (cur.distTo(next) < 1e-5f) continue;   // duplicate of its successor
                out.push_back(cur);
            }
            if (out.size() >= 3) loop.swap(out);
        };
        clean(face.outerLoop);
        for (auto& hole : face.innerLoops) clean(hole);
    }
}

static void capOpenLoops(Solid& solid) {
    removeDegenerateVertices(solid);
    solid.rebuildEdges();

    std::vector<uint32_t> open;
    for (uint32_t i = 0; i < (uint32_t)solid.edges.size(); i++)
        if (solid.edges[i].face1 == NULL_ID) open.push_back(i);
    if (open.empty()) return;

    Vec3 centre{0, 0, 0};
    int nv = 0;
    for (const auto& f : solid.faces)
        for (const auto& p : f.outerLoop) { centre += p; nv++; }
    if (nv == 0) return;
    centre = centre * (1.0f / (float)nv);

    std::vector<bool> used(open.size(), false);
    for (size_t i = 0; i < open.size(); i++) {
        if (used[i]) continue;
        std::vector<Vec3> loop;
        loop.push_back(solid.edges[open[i]].start);
        Vec3 cursor = solid.edges[open[i]].end;
        used[i] = true;

        // Walk from edge to edge through shared endpoints.
        bool closed = false;
        for (size_t guard = 0; guard < open.size(); guard++) {
            if (cursor.distTo(loop.front()) < 1e-4f) { closed = true; break; }
            bool advanced = false;
            for (size_t j = 0; j < open.size(); j++) {
                if (used[j]) continue;
                const BRepEdge& e = solid.edges[open[j]];
                if (e.start.distTo(cursor) < 1e-4f)      { loop.push_back(cursor); cursor = e.end;   used[j] = true; advanced = true; break; }
                else if (e.end.distTo(cursor) < 1e-4f)   { loop.push_back(cursor); cursor = e.start; used[j] = true; advanced = true; break; }
            }
            if (!advanced) break;
        }
        if (!closed || loop.size() < 3) continue;

        // Only cap something small and genuinely flat; anything larger is not a
        // corner gap and guessing at it would do more harm than leaving it.
        if (loop.size() > 12) continue;
        Vec3 n{0, 0, 0};
        for (size_t k = 0; k < loop.size(); k++)
            n += loop[k].cross(loop[(k + 1) % loop.size()]);
        if (n.length() < 1e-9f) continue;
        Vec3 unit = n.normalized();
        bool flat = true;
        for (const Vec3& p : loop)
            if (std::abs(unit.dot(p - loop[0])) > 1e-3f) { flat = false; break; }
        if (!flat) continue;

        BRepFace patch;
        patch.outerLoop = loop;
        orientLoop(patch.outerLoop, patch.outerLoop[0] - centre);   // face outward
        patch.computeNormal();
        patch.color = {0.85f, 0.75f, 0.65f};
        solid.faces.push_back(std::move(patch));
    }
    solid.rebuildEdges();
}

// Chamfer or fillet a closed rim that bounds a hole.
//
// A hole rim cannot be handled edge-by-edge: trimming a face back from an edge
// that sits in the middle of it slices the whole face apart.  The rim has to be
// treated as one closed curve — widen the hole in the flat face by `d`, push
// the bore wall back by `d`, and bridge the two with a ring of faces.
//
// `arcSegs` of 1 gives a flat bevel (chamfer); more gives a rounded one.
// Returns false if the selection is not a closed rim, so the caller can fall
// back to the ordinary per-edge path.
static bool modifyInnerRim(Solid& solid, const std::vector<uint32_t>& edgeIndices,
                           float d, int arcSegs) {
    if (edgeIndices.size() < 3) return false;

    // Chain the selected edges into one closed loop.
    std::vector<Vec3> rim;
    {
        std::vector<std::pair<Vec3, Vec3>> segs;
        for (uint32_t i : edgeIndices) {
            if (i >= solid.edges.size()) return false;
            segs.push_back({ solid.edges[i].start, solid.edges[i].end });
        }
        std::vector<bool> used(segs.size(), false);
        rim.push_back(segs[0].first);
        Vec3 cursor = segs[0].second;
        used[0] = true;
        for (size_t guard = 0; guard < segs.size(); guard++) {
            if (cursor.distTo(rim.front()) < 1e-4f) break;
            bool moved = false;
            for (size_t j = 0; j < segs.size(); j++) {
                if (used[j]) continue;
                if (segs[j].first.distTo(cursor) < 1e-4f)  { rim.push_back(cursor); cursor = segs[j].second; used[j] = true; moved = true; break; }
                if (segs[j].second.distTo(cursor) < 1e-4f) { rim.push_back(cursor); cursor = segs[j].first;  used[j] = true; moved = true; break; }
            }
            if (!moved) return false;
        }
        if (rim.size() != segs.size()) return false;          // not a single closed loop
        if (cursor.distTo(rim.front()) > 1e-4f) return false;
    }

    // Find the flat face carrying this rim as one of its holes.
    int faceIdx = -1, loopIdx = -1;
    for (size_t fi = 0; fi < solid.faces.size() && faceIdx < 0; fi++) {
        for (size_t li = 0; li < solid.faces[fi].innerLoops.size(); li++) {
            const auto& L = solid.faces[fi].innerLoops[li];
            if (L.size() != rim.size()) continue;
            bool all = true;
            for (const Vec3& p : rim) {
                bool hit = false;
                for (const Vec3& q : L) if (p.distTo(q) < 1e-4f) { hit = true; break; }
                if (!hit) { all = false; break; }
            }
            if (all) { faceIdx = (int)fi; loopIdx = (int)li; break; }
        }
    }
    if (faceIdx < 0) return false;

    const Vec3 faceNormal = solid.faces[faceIdx].normal;

    // Work in the face's plane to widen the hole by d.
    Vec3 ref = (std::abs(faceNormal.y) > 0.9f) ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    Vec3 u = faceNormal.cross(ref).normalized();
    Vec3 v = faceNormal.cross(u).normalized();
    Vec3 base = rim[0];
    auto to2 = [&](Vec3 p) { Vec3 q = p - base; return Vec2{ q.dot(u), q.dot(v) }; };
    auto to3 = [&](Vec2 p) { return base + u * p.x + v * p.y; };

    std::vector<Vec2> flat;
    flat.reserve(rim.size());
    for (const Vec3& p : rim) flat.push_back(to2(p));

    auto area2 = [](const std::vector<Vec2>& L) {
        float a = 0;
        for (size_t i = 0; i < L.size(); i++) {
            const Vec2& p = L[i]; const Vec2& q = L[(i + 1) % L.size()];
            a += p.x * q.y - q.x * p.y;
        }
        return std::abs(a) * 0.5f;
    };
    // Take whichever offset direction enlarges the hole.
    std::vector<Vec2> plus = offsetProfile2D(flat,  d);
    std::vector<Vec2> minus = offsetProfile2D(flat, -d);
    const std::vector<Vec2>& wider =
        (area2(plus) > area2(minus)) ? plus : minus;
    if (wider.size() != flat.size()) return false;
    if (area2(wider) <= area2(flat)) return false;            // offset collapsed

    std::vector<Vec3> widened;
    widened.reserve(wider.size());
    for (const Vec2& p : wider) widened.push_back(to3(p));

    // Push the rim back along the bore by d.
    std::vector<Vec3> sunk;
    sunk.reserve(rim.size());
    for (const Vec3& p : rim) sunk.push_back(p - faceNormal * d);

    // Widen the hole in the flat face.
    solid.faces[faceIdx].innerLoops[loopIdx] = widened;

    // Move the wall faces' rim vertices down to match.
    for (auto& f : solid.faces) {
        if (&f == &solid.faces[faceIdx]) continue;
        for (auto& p : f.outerLoop) {
            for (size_t k = 0; k < rim.size(); k++) {
                if (p.distTo(rim[k]) < 1e-4f) { p = sunk[k]; break; }
            }
        }
    }

    // Ring of faces bridging the widened hole edge to the sunk bore edge.
    int group = solid.nextFaceGroup++;
    size_t n = rim.size();
    for (size_t i = 0; i < n; i++) {
        size_t j = (i + 1) % n;
        for (int seg = 0; seg < arcSegs; seg++) {
            float t0 = (float)seg / arcSegs;
            float t1 = (float)(seg + 1) / arcSegs;
            auto blend = [&](size_t idx, float t) {
                if (arcSegs == 1) return widened[idx] * (1.0f - t) + sunk[idx] * t;
                // Quadratic Bezier through the original rim corner: tangent to
                // the flat face at one end and to the bore wall at the other,
                // and exactly on both endpoints.  A true circular arc of radius
                // d does not land on them, because mitring the hole outward
                // moves by d/cos(half angle) rather than by d.
                float m = 1.0f - t;
                return widened[idx] * (m * m) + rim[idx] * (2.0f * m * t) + sunk[idx] * (t * t);
            };
            BRepFace band;
            band.outerLoop = { blend(i, t0), blend(j, t0), blend(j, t1), blend(i, t1) };
            band.computeNormal();
            band.color = {0.85f, 0.75f, 0.65f};
            band.faceGroup = (arcSegs > 1) ? group : 0;
            solid.faces.push_back(std::move(band));
        }
    }

    solid.computeAllNormals();
    solid.rebuildEdges();
    return true;
}

Solid chamfer(const Solid& solid, const std::vector<uint32_t>& edgeIndices, float distance) {
    distance = std::abs(distance);
    if (distance < 1e-4f) return solid;

    Solid result = solid;
    // A closed hole rim needs whole-curve treatment, not edge-by-edge.
    if (modifyInnerRim(result, edgeIndices, distance, 1)) return result;

    std::vector<std::pair<Vec3, Vec3>> targets;
    for (uint32_t ei : edgeIndices) {
        if (ei < solid.edges.size()) {
            targets.push_back({solid.edges[ei].start, solid.edges[ei].end});
        }
    }

    for (auto& target : targets) {
        result.rebuildEdges();
        int foundEdge = findEdgeIndexByEndpoints(result, target.first, target.second);
        if (foundEdge < 0) continue;

        const BRepEdge edge = result.edges[foundEdge];
        if (edge.face0 == NULL_ID || edge.face1 == NULL_ID) continue;
        if (edge.face0 >= result.faces.size() || edge.face1 >= result.faces.size()) continue;

        const BRepFace f0 = result.faces[edge.face0];
        const BRepFace f1 = result.faces[edge.face1];

        std::vector<Vec3> trim0, trim1;
        Vec3 in0, in1;
        if (!clipFaceByOffsetFromEdge(f0, edge.start, edge.end, distance, trim0, in0)) continue;
        if (!clipFaceByOffsetFromEdge(f1, edge.start, edge.end, distance, trim1, in1)) continue;

        BRepFace nf0 = f0;
        nf0.outerLoop = trim0;
        nf0.computeNormal();

        BRepFace nf1 = f1;
        nf1.outerLoop = trim1;
        nf1.computeNormal();

        BRepFace chamferFace;
        chamferFace.outerLoop.push_back(edge.start + in0 * distance);
        chamferFace.outerLoop.push_back(edge.end + in0 * distance);
        chamferFace.outerLoop.push_back(edge.end + in1 * distance);
        chamferFace.outerLoop.push_back(edge.start + in1 * distance);
        chamferFace.computeNormal();

        // in0 and in1 point inward along their own faces, so away from both is
        // the direction the bevel must face.  Without this the quad's winding
        // depends on the edge's orientation and the bevel can end up facing
        // into the solid — the shape looks right but the body is inside-out
        // there, which is exactly the volume error this used to produce.
        Vec3 outward = -(in0 + in1);
        if (chamferFace.normal.dot(outward) < 0.0f) {
            std::reverse(chamferFace.outerLoop.begin(), chamferFace.outerLoop.end());
            chamferFace.computeNormal();
        }
        chamferFace.color = {0.85f, 0.75f, 0.65f};

        result.faces[edge.face0] = nf0;
        result.faces[edge.face1] = nf1;
        result.faces.push_back(chamferFace);

        // Splice trim points into adjacent perpendicular side faces so they
        // close up around the bevel instead of leaving floating sharp corners.
        std::vector<Vec3> startInset = {edge.start + in0 * distance, edge.start + in1 * distance};
        std::vector<Vec3> endInset   = {edge.end   + in0 * distance, edge.end   + in1 * distance};
        spliceCornerOnSideFaces(result, edge.face0, edge.face1, edge.start, startInset, f0, f1);
        spliceCornerOnSideFaces(result, edge.face0, edge.face1, edge.end,   endInset,   f0, f1);
    }

    result.computeAllNormals();
    capOpenLoops(result);
    return result;
}

// ---- Fillet (trim adjacent faces, insert rounded strip approximating arc) ----
Solid fillet(const Solid& solid, const std::vector<uint32_t>& edgeIndices, float radius) {
    radius = std::abs(radius);
    if (radius < 1e-4f) return solid;

    Solid result = solid;
    const int arcSegs = 10;
    if (modifyInnerRim(result, edgeIndices, radius, arcSegs)) return result;

    std::vector<std::pair<Vec3, Vec3>> targets;
    for (uint32_t ei : edgeIndices) {
        if (ei < solid.edges.size()) {
            targets.push_back({solid.edges[ei].start, solid.edges[ei].end});
        }
    }

    for (auto& target : targets) {
        result.rebuildEdges();
        int foundEdge = findEdgeIndexByEndpoints(result, target.first, target.second);
        if (foundEdge < 0) continue;

        const BRepEdge edge = result.edges[foundEdge];
        if (edge.face0 == NULL_ID || edge.face1 == NULL_ID) continue;
        if (edge.face0 >= result.faces.size() || edge.face1 >= result.faces.size()) continue;

        const BRepFace f0 = result.faces[edge.face0];
        const BRepFace f1 = result.faces[edge.face1];

        std::vector<Vec3> trim0, trim1;
        Vec3 in0, in1;
        if (!clipFaceByOffsetFromEdge(f0, edge.start, edge.end, radius, trim0, in0)) continue;
        if (!clipFaceByOffsetFromEdge(f1, edge.start, edge.end, radius, trim1, in1)) continue;

        BRepFace nf0 = f0;
        nf0.outerLoop = trim0;
        nf0.computeNormal();

        BRepFace nf1 = f1;
        nf1.outerLoop = trim1;
        nf1.computeNormal();

        result.faces[edge.face0] = nf0;
        result.faces[edge.face1] = nf1;

        // All strip faces produced for this fillet edge share a fresh face
        // group id so picking can later collapse the rim's straight segments
        // into a single logical curved edge.
        int filletGroup = result.nextFaceGroup++;

        // Proper circular arc around the fillet center C = edge + (in0+in1)*r.
        // The arc passes through P0 = edge + in0*r and P1 = edge + in1*r,
        // bulging on the correct side (toward the original sharp corner for
        // convex edges, into the open corner for concave edges).  The previous
        // lerp-based parameterization put the arc on the wrong side of the
        // chord and produced a concave scoop instead of a convex round.
        Vec3 a = -in1;
        Vec3 b = -in0;
        float dotab = std::max(-1.0f, std::min(1.0f, a.dot(b)));
        float omega = std::acos(dotab);
        float sinOmega = std::sin(omega);

        std::vector<Vec3> arcDirs;  // each entry d satisfies arcPoint = edge + d * radius
        arcDirs.reserve(arcSegs + 1);
        for (int i = 0; i <= arcSegs; i++) {
            float t = (float)i / arcSegs;
            Vec3 unit;
            if (sinOmega < EPSILON) {
                unit = a;
            } else {
                unit = a * (std::sin((1.0f - t) * omega) / sinOmega)
                     + b * (std::sin(t * omega) / sinOmega);
            }
            arcDirs.push_back((in0 + in1) + unit);
        }

        for (int i = 0; i < arcSegs; i++) {
            const Vec3& v0 = arcDirs[i];
            const Vec3& v1 = arcDirs[i + 1];

            BRepFace strip;
            strip.outerLoop.push_back(edge.start + v0 * radius);
            strip.outerLoop.push_back(edge.end + v0 * radius);
            strip.outerLoop.push_back(edge.end + v1 * radius);
            strip.outerLoop.push_back(edge.start + v1 * radius);
            strip.computeNormal();

            // arcDirs hold (in0+in1) + unit, so subtracting the centre offset
            // recovers the outward radial direction of the round.  Same
            // correction as the chamfer bevel: without it the strip winding
            // follows the edge's orientation and the round can face into the
            // solid, which reads as a valid shape but an inside-out body.
            Vec3 radial = (v0 - (in0 + in1)) + (v1 - (in0 + in1));
            if (strip.normal.dot(radial) < 0.0f) {
                std::reverse(strip.outerLoop.begin(), strip.outerLoop.end());
                strip.computeNormal();
            }
            strip.color = {0.75f, 0.78f, 0.85f};
            strip.faceGroup = filletGroup;
            result.faces.push_back(strip);
        }

        // Splice the arc points into adjacent perpendicular side faces so the
        // round meets the side cleanly (instead of the side keeping its sharp
        // corner over a floating arc).
        std::vector<Vec3> startInset, endInset;
        startInset.reserve(arcDirs.size());
        endInset.reserve(arcDirs.size());
        for (const auto& v : arcDirs) {
            startInset.push_back(edge.start + v * radius);
            endInset.push_back(edge.end + v * radius);
        }
        spliceCornerOnSideFaces(result, edge.face0, edge.face1, edge.start, startInset, f0, f1);
        spliceCornerOnSideFaces(result, edge.face0, edge.face1, edge.end,   endInset,   f0, f1);
    }

    result.computeAllNormals();
    capOpenLoops(result);
    return result;
}

// ---- Split (with cap faces) ----
std::pair<Solid, Solid> split(const Solid& solid, Plane plane) {
    Solid above, below;
    std::vector<Vec3> capPointsAbove, capPointsBelow;

    for (const auto& face : solid.faces) {
        std::vector<float> dists;
        for (const auto& p : face.outerLoop)
            dists.push_back(plane.distTo(p));

        bool hasAbove = false, hasBelow = false;
        for (float d : dists) {
            if (d > EPSILON) hasAbove = true;
            if (d < -EPSILON) hasBelow = true;
        }

        if (!hasBelow) {
            above.faces.push_back(face);
        } else if (!hasAbove) {
            below.faces.push_back(face);
        } else {
            BRepFace faceAbove, faceBelow;
            size_t n = face.outerLoop.size();
            for (size_t i = 0; i < n; i++) {
                size_t j = (i + 1) % n;
                Vec3 pi = face.outerLoop[i];
                Vec3 pj = face.outerLoop[j];
                float di = dists[i], dj = dists[j];

                if (di >= -EPSILON) faceAbove.outerLoop.push_back(pi);
                if (di <= EPSILON) faceBelow.outerLoop.push_back(pi);

                if ((di > EPSILON && dj < -EPSILON) || (di < -EPSILON && dj > EPSILON)) {
                    float t = di / (di - dj);
                    Vec3 intersection = lerp(pi, pj, t);
                    faceAbove.outerLoop.push_back(intersection);
                    faceBelow.outerLoop.push_back(intersection);
                    capPointsAbove.push_back(intersection);
                    capPointsBelow.push_back(intersection);
                }
            }

            if (faceAbove.outerLoop.size() >= 3) {
                faceAbove.normal = face.normal;
                faceAbove.color = face.color;
                above.faces.push_back(faceAbove);
            }
            if (faceBelow.outerLoop.size() >= 3) {
                faceBelow.normal = face.normal;
                faceBelow.color = face.color;
                below.faces.push_back(faceBelow);
            }
        }
    }

    // Create cap faces from intersection points
    if (capPointsAbove.size() >= 3) {
        // Sort points around centroid to form a proper polygon
        Vec3 centroid;
        for (const auto& p : capPointsAbove) centroid += p;
        centroid = centroid / (float)capPointsAbove.size();

        // Remove duplicate points
        std::vector<Vec3> unique;
        for (const auto& p : capPointsAbove) {
            bool dup = false;
            for (const auto& u : unique)
                if (p.distTo(u) < EPSILON * 100) { dup = true; break; }
            if (!dup) unique.push_back(p);
        }

        if (unique.size() >= 3) {
            // Sort by angle around centroid projected onto the plane
            Vec3 n = plane.normal;
            Vec3 ref = (unique[0] - centroid).normalized();
            Vec3 binormal = n.cross(ref).normalized();

            std::sort(unique.begin(), unique.end(), [&](Vec3 a, Vec3 b) {
                Vec3 da = a - centroid, db = b - centroid;
                float angA = std::atan2(da.dot(binormal), da.dot(ref));
                float angB = std::atan2(db.dot(binormal), db.dot(ref));
                return angA < angB;
            });

            // The two caps were the wrong way round.  "above" is the piece on
            // the +normal side, so the cut face closing it has to point back
            // along -normal, and vice versa.  Orienting each loop explicitly
            // (rather than trusting the sort's handedness) makes it correct
            // regardless of which way the sort happened to wind.
            //
            // This stayed hidden for a long time because an inverted face only
            // shifts the computed volume by twice its own contribution, and
            // that contribution is zero when the cut plane passes through the
            // origin — which is exactly where a cube gets split in most tests.
            BRepFace capAbove;
            capAbove.outerLoop = unique;
            orientLoop(capAbove.outerLoop, -plane.normal);
            capAbove.computeNormal();
            capAbove.color = {0.7f, 0.7f, 0.8f};
            above.faces.push_back(capAbove);

            BRepFace capBelow;
            capBelow.outerLoop = unique;
            orientLoop(capBelow.outerLoop, plane.normal);
            capBelow.computeNormal();
            capBelow.color = {0.7f, 0.7f, 0.8f};
            below.faces.push_back(capBelow);
        }
    }

    above.computeAllNormals();
    below.computeAllNormals();
    above.rebuildEdges();
    below.rebuildEdges();
    return {above, below};
}



// ---- Offset Profile 2D ----
std::vector<Vec2> offsetProfile2D(const std::vector<Vec2>& profile, float distance) {
    int n = (int)profile.size();
    if (n < 3) return profile;

    std::vector<Vec2> result(n);
    for (int i = 0; i < n; i++) {
        Vec2 prev = profile[(i + n - 1) % n];
        Vec2 curr = profile[i];
        Vec2 next = profile[(i + 1) % n];

        Vec2 e1 = (curr - prev).normalized();
        Vec2 e2 = (next - curr).normalized();
        Vec2 n1 = e1.perp(); // outward normal
        Vec2 n2 = e2.perp();

        // Mitre: travel along the bisector by distance / cos(half angle), so
        // both adjacent edges end up exactly `distance` from their originals.
        // This used the *cross* product, which is ~0 for a gentle turn, so the
        // offset blew up and was clamped — collapsing the whole profile.  A
        // 32-gon offset outward came back smaller than it started.
        Vec2 bisector = (n1 + n2).normalized();
        if (bisector.lengthSq() < EPSILON) { result[i] = curr + n1 * distance; continue; }
        float cosHalf = n1.dot(bisector);
        float offsetLen = (std::abs(cosHalf) > 1e-3f) ? distance / cosHalf : distance;
        // Guard only against genuine spikes, not ordinary corners.
        float limit = std::abs(distance) * 10.0f;
        offsetLen = std::max(-limit, std::min(limit, offsetLen));
        result[i] = curr + bisector * offsetLen;
    }
    return result;
}

// ---- Primitives ----
Solid makeBox(Vec3 center, Vec3 size) {
    Vec3 h = size * 0.5f;
    Vec3 v[8] = {
        center + Vec3{-h.x, -h.y, -h.z},
        center + Vec3{ h.x, -h.y, -h.z},
        center + Vec3{ h.x,  h.y, -h.z},
        center + Vec3{-h.x,  h.y, -h.z},
        center + Vec3{-h.x, -h.y,  h.z},
        center + Vec3{ h.x, -h.y,  h.z},
        center + Vec3{ h.x,  h.y,  h.z},
        center + Vec3{-h.x,  h.y,  h.z},
    };

    Solid s;
    auto addFace = [&](std::vector<Vec3> loop, Vec3 n) {
        BRepFace f;
        f.outerLoop = std::move(loop);
        f.normal = n;
        s.faces.push_back(std::move(f));
    };
    addFace({v[4], v[5], v[6], v[7]}, {0, 0, 1});
    addFace({v[1], v[0], v[3], v[2]}, {0, 0, -1});
    addFace({v[5], v[1], v[2], v[6]}, {1, 0, 0});
    addFace({v[0], v[4], v[7], v[3]}, {-1, 0, 0});
    addFace({v[7], v[6], v[2], v[3]}, {0, 1, 0});
    addFace({v[0], v[1], v[5], v[4]}, {0, -1, 0});

    s.rebuildEdges();
    return s;
}

void translateSolid(Solid& solid, Vec3 delta) {
    for (auto& face : solid.faces) {
        for (auto& p : face.outerLoop) p += delta;
        for (auto& hole : face.innerLoops)
            for (auto& p : hole) p += delta;
    }
    for (auto& e : solid.edges) { e.start += delta; e.end += delta; }
}

float measureDistance(Vec3 a, Vec3 b) {
    return a.distTo(b);
}

float measureAngle(Vec3 edgeDir1, Vec3 edgeDir2) {
    float d = edgeDir1.normalized().dot(edgeDir2.normalized());
    d = std::max(-1.0f, std::min(1.0f, d));
    return std::acos(d) * RAD2DEG;
}

} // namespace cad
