#pragma once
#include "cad_math.h"
#include <vector>
#include <cstdint>
#include <memory>

namespace cad {

// Forward declarations
struct HalfEdge;
struct Edge;
struct Face;
struct Solid;

using VertexId = uint32_t;
using EdgeId   = uint32_t;
using FaceId   = uint32_t;
using SolidId  = uint32_t;

constexpr uint32_t NULL_ID = UINT32_MAX;

// ---- Triangle Mesh (for rendering) ----
struct Mesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<uint32_t> indices;

    void clear() { positions.clear(); normals.clear(); indices.clear(); }

    void addTriangle(Vec3 a, Vec3 b, Vec3 c) {
        Vec3 n = (b - a).cross(c - a).normalized();
        uint32_t base = (uint32_t)positions.size();
        positions.push_back(a); normals.push_back(n);
        positions.push_back(b); normals.push_back(n);
        positions.push_back(c); normals.push_back(n);
        indices.push_back(base);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
    }

    void addQuad(Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
        addTriangle(a, b, c);
        addTriangle(a, c, d);
    }

    AABB bounds() const {
        AABB bb;
        for (auto& p : positions) bb.expand(p);
        return bb;
    }
};

// ---- Wire (list of line segments for edges/sketch) ----
struct Wire {
    std::vector<Vec3> points; // connected polyline; closed if first == last
    bool closed = false;

    void close() {
        if (points.size() >= 2 && points.front().distTo(points.back()) > EPSILON) {
            points.push_back(points.front());
        }
        closed = true;
    }
};

// ---- B-Rep Face (flat polygon for now) ----
struct BRepFace {
    std::vector<Vec3> outerLoop; // ordered polygon vertices
    Vec3 normal;
    // Holes cut out of this face, each a closed loop lying in the same plane.
    // A face without these cannot represent a bore, which previously forced
    // extrudeWithHoles to stack a reversed polygon over the cap instead of
    // cutting it — geometry that looks right on screen but exports solid.
    // Declared after `normal` so `{loop, normal}` aggregate init still works.
    std::vector<std::vector<Vec3>> innerLoops;
    Color color{0.7f, 0.7f, 0.8f, 1.0f};
    // Faces produced by the same curved-surface operation (e.g., the strip
    // faces of a fillet) share a nonzero faceGroup id so picking can treat the
    // result as a single logical curved edge instead of N straight segments.
    int faceGroup = 0;

    void computeNormal() {
        if (outerLoop.size() < 3) { normal = {0,1,0}; return; }
        // Newell's method for robust normal
        normal = {0,0,0};
        for (size_t i = 0; i < outerLoop.size(); i++) {
            const Vec3& cur = outerLoop[i];
            const Vec3& next = outerLoop[(i + 1) % outerLoop.size()];
            normal.x += (cur.y - next.y) * (cur.z + next.z);
            normal.y += (cur.z - next.z) * (cur.x + next.x);
            normal.z += (cur.x - next.x) * (cur.y + next.y);
        }
        normal = normal.normalized();
    }

    Vec3 centroid() const {
        Vec3 c;
        for (auto& p : outerLoop) c += p;
        if (!outerLoop.empty()) c = c / (float)outerLoop.size();
        return c;
    }

    Plane plane() const { return {normal, outerLoop.empty() ? Vec3{} : outerLoop[0]}; }
};

// ---- B-Rep Edge ----
struct BRepEdge {
    Vec3 start, end;
    FaceId face0 = NULL_ID, face1 = NULL_ID; // adjacent faces

    Vec3 midpoint() const { return (start + end) * 0.5f; }
    float length() const { return start.distTo(end); }
    Vec3 direction() const { return (end - start).normalized(); }
};

// ---- Solid (collection of faces + edges) ----
struct Solid {
    std::vector<BRepFace> faces;
    std::vector<BRepEdge> edges;
    Color color{0.6f, 0.65f, 0.75f, 1.0f};
    bool visible = true;
    // Counter used to allocate fresh face-group ids.  Operations that produce
    // groups of related faces (fillet strips, future revolve segments, ...)
    // increment this to get a unique id.
    int nextFaceGroup = 1;

    void computeAllNormals() {
        for (auto& f : faces) f.computeNormal();
    }

    // Rebuild edge list from faces
    void rebuildEdges();

    // Given an edge index, return all edges that belong to the same logical
    // curved feature.  Two cases:
    //   * Both adjacent faces share a nonzero faceGroup -> interior seam:
    //     returns every interior seam of that group.
    //   * Exactly one adjacent face has a nonzero faceGroup, the other is a
    //     specific flat face -> rim: returns every edge bordering the same
    //     (group, flat-face) pair.  This is exactly the polyline that
    //     approximates one side of the curved boundary, so callers can treat
    //     it as a single curved edge.
    // Edges that are not part of any group simply return { edgeIdx }.
    std::vector<uint32_t> curveGroupEdges(uint32_t edgeIdx) const;

    // Label every face with the smooth surface it belongs to: faces joined
    // across a seam shallower than the crease angle are one surface.  A
    // tessellated bore is one surface, a box corner is three.
    std::vector<int> smoothSurfaces() const;

    // Every face belonging to the same logical planar region as `faceIdx`:
    // the set of connected, coplanar neighbours reached by walking shared
    // edges.  A boolean shatters one flat face into many fragments, so a
    // single BRepFace is not what a user means by "a face" — selection,
    // highlighting and extrude all need the region instead.
    std::vector<uint32_t> coplanarRegion(uint32_t faceIdx) const;

    // Outline of a coplanar region: the loops left once every seam interior to
    // the region cancels out.  Element 0 is the outer boundary (largest area);
    // any further loops are holes.  Empty if the region is not a clean sheet.
    std::vector<std::vector<Vec3>> regionBoundaryLoops(const std::vector<uint32_t>& region) const;

    // Tessellate all faces into a triangle mesh for rendering
    Mesh tessellate() const;

    // Get all edges as line segments for wireframe rendering
    Wire wireframe() const;

    AABB bounds() const;
};

// ---- CAD Operations ----

// Extrude a closed 2D polygon (in 3D) along a direction by distance
Solid extrude(const std::vector<Vec3>& profile, Vec3 direction, float distance);

// Extrude a profile with holes (outer boundary + inner cutouts) along a direction
Solid extrudeWithHoles(const std::vector<Vec3>& outerProfile,
                       const std::vector<std::vector<Vec3>>& holes,
                       Vec3 direction, float distance);

// Test if a 2D point is inside a closed 2D polygon (ray casting)
bool pointInPolygon2D(Vec2 point, const std::vector<Vec2>& polygon);

// Chamfer specific edges of a solid (properly trims original faces)
Solid chamfer(const Solid& solid, const std::vector<uint32_t>& edgeIndices, float distance);

// Fillet edges with arc approximation
Solid fillet(const Solid& solid, const std::vector<uint32_t>& edgeIndices, float radius);

// Split a solid by a plane — returns two solids (with cap faces)
std::pair<Solid, Solid> split(const Solid& solid, Plane plane);

// Offset a closed 2D profile inward/outward
std::vector<Vec2> offsetProfile2D(const std::vector<Vec2>& profile, float distance);

// Create a box primitive
Solid makeBox(Vec3 center, Vec3 size);

// Translate every vertex of a solid, inner loops included.  Anything that moves
// geometry must touch innerLoops too, or a body with a bore leaves its holes
// behind at the old position.
void translateSolid(Solid& solid, Vec3 delta);

// Measure distance between two points
float measureDistance(Vec3 a, Vec3 b);

// Measure angle between two edges (in degrees)
float measureAngle(Vec3 edgeDir1, Vec3 edgeDir2);

// Ear-clipping triangulation for potentially concave polygons
void triangulatePolygon(const std::vector<Vec3>& polygon, Vec3 normal,
                        std::vector<Vec3>& outPositions, std::vector<Vec3>& outNormals,
                        std::vector<uint32_t>& outIndices, uint32_t baseIndex = 0);

// Splice inner loops into the outer loop with bridge cuts, yielding one simple
// polygon that ear-clipping can consume.  Returns the outer loop unchanged when
// there are no holes.
std::vector<Vec3> bridgeHoles(const std::vector<Vec3>& outer,
                              const std::vector<std::vector<Vec3>>& holes,
                              Vec3 normal);

} // namespace cad
