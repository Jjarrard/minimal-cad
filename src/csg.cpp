#include "csg.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <unordered_map>

using namespace cad;

namespace {

const float EPS = 1e-5f;

enum { COPLANAR = 0, FRONT = 1, BACK = 2, SPANNING = 3 };

struct Polygon {
    std::vector<Vec3> vertices;
    Vec3 normal;
    // Which face of which input solid this came from.  BSP splitting shatters
    // one flat face into many fragments; carrying the origin lets them be
    // stitched back into a single face afterwards, instead of leaving the
    // result as a soup where "the top face" is a hundred slivers.
    int   srcFace = -1;
    Color srcColor{0.7f, 0.7f, 0.8f, 1.0f};
    int   srcGroup = 0;

    void flip() {
        std::reverse(vertices.begin(), vertices.end());
        normal = -normal;
    }
};

// ---------- BSP tree node ----------

struct Node {
    Plane splitPlane{{0,1,0}, 0};
    bool hasPlane = false;
    std::vector<Polygon> polygons;
    Node* front = nullptr;
    Node* back  = nullptr;

    Node() = default;
    explicit Node(const std::vector<Polygon>& list) { build(list); }
    ~Node() { delete front; delete back; }

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    // ---- flip inside / outside ----
    void invert() {
        for (auto& p : polygons) p.flip();
        splitPlane.normal = -splitPlane.normal;
        splitPlane.d      = -splitPlane.d;
        std::swap(front, back);
        if (front) front->invert();
        if (back)  back->invert();
    }

    // ---- collect every polygon in the tree ----
    std::vector<Polygon> allPolygons() const {
        auto result = polygons;
        if (front) {
            auto f = front->allPolygons();
            result.insert(result.end(), f.begin(), f.end());
        }
        if (back) {
            auto b = back->allPolygons();
            result.insert(result.end(), b.begin(), b.end());
        }
        return result;
    }

    // ---- clip a polygon list against this BSP tree ----
    //   keeps "front" (outside) polygons, discards "back" (inside) when
    //   there is no back child
    std::vector<Polygon> clipPolygons(const std::vector<Polygon>& list) const {
        if (!hasPlane) return list;

        std::vector<Polygon> fl, bl;
        for (auto& poly : list)
            splitPolygon(poly, fl, bl, fl, bl);

        fl = front ? front->clipPolygons(fl) : fl;
        bl = back  ? back->clipPolygons(bl)  : std::vector<Polygon>{};

        fl.insert(fl.end(), bl.begin(), bl.end());
        return fl;
    }

    // ---- clip every polygon stored in *this* tree against another BSP ----
    void clipTo(const Node* bsp) {
        polygons = bsp->clipPolygons(polygons);
        if (front) front->clipTo(bsp);
        if (back)  back->clipTo(bsp);
    }

    // ---- insert polygons into the tree ----
    void build(const std::vector<Polygon>& list) {
        if (list.empty()) return;
        if (!hasPlane) {
            splitPlane = Plane(list[0].normal, list[0].vertices[0]);
            hasPlane   = true;
        }
        std::vector<Polygon> fl, bl;
        for (auto& poly : list)
            splitPolygon(poly, polygons, polygons, fl, bl);

        if (!fl.empty()) { if (!front) front = new Node(); front->build(fl); }
        if (!bl.empty()) { if (!back)  back  = new Node(); back->build(bl);  }
    }

    // ---- classify & split a polygon against this node's plane ----
    void splitPolygon(const Polygon& poly,
                      std::vector<Polygon>& coplanarFront,
                      std::vector<Polygon>& coplanarBack,
                      std::vector<Polygon>& frontList,
                      std::vector<Polygon>& backList) const
    {
        int polyType = 0;
        std::vector<int> types;
        types.reserve(poly.vertices.size());

        for (auto& v : poly.vertices) {
            float d = splitPlane.distTo(v);
            int t = (d < -EPS) ? BACK : (d > EPS) ? FRONT : COPLANAR;
            polyType |= t;
            types.push_back(t);
        }

        switch (polyType) {
        case COPLANAR:
            (splitPlane.normal.dot(poly.normal) > 0
                ? coplanarFront : coplanarBack).push_back(poly);
            break;
        case FRONT:
            frontList.push_back(poly);
            break;
        case BACK:
            backList.push_back(poly);
            break;
        case SPANNING: {
            std::vector<Vec3> f, b;
            size_t n = poly.vertices.size();
            for (size_t i = 0; i < n; i++) {
                size_t j = (i + 1) % n;
                int ti = types[i], tj = types[j];
                Vec3 vi = poly.vertices[i], vj = poly.vertices[j];

                if (ti != BACK)  f.push_back(vi);
                if (ti != FRONT) b.push_back(vi);

                if ((ti | tj) == SPANNING) {
                    float di = splitPlane.distTo(vi);
                    float dj = splitPlane.distTo(vj);
                    float denom = di - dj;
                    if (std::abs(denom) < 1e-8f) continue;  // degenerate edge, skip
                    float t  = di / denom;
                    Vec3 hit = vi + (vj - vi) * t;
                    f.push_back(hit);
                    b.push_back(hit);
                }
            }
            if (f.size() >= 3) {
                Polygon fp = poly; fp.vertices = f;
                frontList.push_back(fp);
            }
            if (b.size() >= 3) {
                Polygon bp = poly; bp.vertices = b;
                backList.push_back(bp);
            }
            break;
        }
        }
    }
};

// ---------- conversion helpers ----------

std::vector<Polygon> solidToPolygons(const Solid& s, int idBase) {
    std::vector<Polygon> out;
    out.reserve(s.faces.size());
    for (size_t fi = 0; fi < s.faces.size(); fi++) {
        const auto& face = s.faces[fi];
        if (face.outerLoop.size() < 3) continue;

        const int id = idBase + (int)fi;

        if (face.innerLoops.empty()) {
            Polygon p;
            p.vertices = face.outerLoop;
            p.normal   = face.normal;
            p.srcFace  = id;
            p.srcColor = face.color;
            p.srcGroup = face.faceGroup;
            out.push_back(p);
            continue;
        }

        // A BSP node splits convex-ish polygons and has no concept of a hole,
        // so a face with a bore has to enter the tree pre-triangulated.
        std::vector<Vec3> pos, nrm;
        std::vector<uint32_t> idx;
        triangulatePolygon(bridgeHoles(face.outerLoop, face.innerLoops, face.normal),
                           face.normal, pos, nrm, idx, 0);
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            if (idx[i] >= pos.size() || idx[i+1] >= pos.size() || idx[i+2] >= pos.size()) continue;
            Polygon p;
            p.vertices = { pos[idx[i]], pos[idx[i+1]], pos[idx[i+2]] };
            p.normal   = face.normal;
            p.srcFace  = id;
            p.srcColor = face.color;
            p.srcGroup = face.faceGroup;
            out.push_back(p);
        }
    }
    return out;
}

// Reorder a loop so its Newell normal points along `dir`.
void orientLoopTo(std::vector<Vec3>& loop, Vec3 dir) {
    if (loop.size() < 3) return;
    Vec3 n{0, 0, 0};
    for (size_t i = 0; i < loop.size(); i++)
        n += loop[i].cross(loop[(i + 1) % loop.size()]);
    if (n.dot(dir) < 0.0f) std::reverse(loop.begin(), loop.end());
}

Solid polygonsToSolid(const std::vector<Polygon>& polys, Color col) {
    // Stage one: one face per surviving polygon, remembering where each came
    // from.  Stage two below stitches same-origin fragments back together.
    Solid scratch;
    std::vector<int>   origin;
    std::vector<Color> colours;
    std::vector<int>   groups;
    std::vector<Vec3>  normals;
    for (auto& poly : polys) {
        if (poly.vertices.size() < 3) continue;
        BRepFace face;
        face.outerLoop = poly.vertices;
        face.normal    = poly.normal;
        face.color     = poly.srcColor;
        face.faceGroup = poly.srcGroup;
        scratch.faces.push_back(face);
        origin.push_back(poly.srcFace);
        colours.push_back(poly.srcColor);
        groups.push_back(poly.srcGroup);
        normals.push_back(poly.normal);
    }

    // Group the fragments by originating face.  Two fragments of the same
    // source face can end up on opposite sides of the result (a cut can leave
    // a face in two genuinely separate pieces), so each group is further split
    // into connected regions before its outline is recovered.
    std::unordered_map<int, std::vector<uint32_t>> byOrigin;
    for (uint32_t i = 0; i < (uint32_t)scratch.faces.size(); i++)
        if (origin[i] >= 0) byOrigin[origin[i]].push_back(i);

    Solid result;
    std::vector<bool> consumed(scratch.faces.size(), false);

    for (auto& entry : byOrigin) {
        const std::vector<uint32_t>& all = entry.second;
        if (all.size() < 2) continue;   // nothing to merge

        for (uint32_t seed : all) {
            if (consumed[seed]) continue;
            std::vector<uint32_t> region = scratch.coplanarRegion(seed);

            // Keep only members of this same source face.
            std::vector<uint32_t> mine;
            for (uint32_t fi : region)
                if (origin[fi] == entry.first && !consumed[fi]) mine.push_back(fi);
            if (mine.size() < 2) continue;

            auto loops = scratch.regionBoundaryLoops(mine);
            if (loops.empty() || loops[0].size() < 3) continue;   // fall back to fragments

            BRepFace merged;
            merged.outerLoop = loops[0];
            orientLoopTo(merged.outerLoop, normals[seed]);
            for (size_t li = 1; li < loops.size(); li++) {
                std::vector<Vec3> hole = loops[li];
                orientLoopTo(hole, -normals[seed]);
                merged.innerLoops.push_back(std::move(hole));
            }
            merged.normal    = normals[seed];
            merged.color     = colours[seed];
            merged.faceGroup = groups[seed];
            result.faces.push_back(std::move(merged));

            for (uint32_t fi : mine) consumed[fi] = true;
        }
    }

    // Anything not folded into a merged face is carried over unchanged.
    for (uint32_t i = 0; i < (uint32_t)scratch.faces.size(); i++)
        if (!consumed[i]) result.faces.push_back(scratch.faces[i]);

    result.color = col;
    result.computeAllNormals();
    result.rebuildEdges();
    return result;
}

} // anonymous namespace

// ---------- public API ----------

namespace cad {

Solid csgUnion(const Solid& a, const Solid& b) {
    auto ap = solidToPolygons(a, 0);
    auto bp = solidToPolygons(b, 1000000);
    if (ap.empty()) return b;
    if (bp.empty()) return a;

    Node na(ap);
    Node nb(bp);

    na.clipTo(&nb);
    nb.clipTo(&na);
    nb.invert();
    nb.clipTo(&na);
    nb.invert();
    na.build(nb.allPolygons());

    return polygonsToSolid(na.allPolygons(), a.color);
}

Solid csgSubtract(const Solid& a, const Solid& b) {
    auto ap = solidToPolygons(a, 0);
    auto bp = solidToPolygons(b, 1000000);
    if (ap.empty()) return a;
    if (bp.empty()) return a;

    Node na(ap);
    Node nb(bp);

    // Canonical BSP difference.  The previous sequence omitted the first
    // a.clipTo(b) and inverted b in the wrong place, which made this return
    // A ∩ B rather than A − B.  It went unnoticed because extrude() used to
    // emit inside-out solids, and the two faults cancelled.
    na.invert();
    na.clipTo(&nb);
    nb.clipTo(&na);
    nb.invert();
    nb.clipTo(&na);
    nb.invert();
    na.build(nb.allPolygons());
    na.invert();

    return polygonsToSolid(na.allPolygons(), a.color);
}

Solid csgIntersect(const Solid& a, const Solid& b) {
    auto ap = solidToPolygons(a, 0);
    auto bp = solidToPolygons(b, 1000000);
    if (ap.empty() || bp.empty()) return Solid{};

    Node na(ap);
    Node nb(bp);

    // A ∩ B = complement( complement(A) ∪ complement(B) )
    na.invert();
    nb.invert();
    // union of complements:
    na.clipTo(&nb);
    nb.clipTo(&na);
    nb.invert();
    nb.clipTo(&na);
    nb.invert();
    na.build(nb.allPolygons());
    // complement the result:
    na.invert();

    return polygonsToSolid(na.allPolygons(), a.color);
}

} // namespace cad
