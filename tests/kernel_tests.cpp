// Invariant tests for the geometry kernel.
//
// Every operation here is a pure function with a checkable answer, and the ways
// they fail are silent and geometric rather than loud: an inverted face still
// renders, a dropped hole still looks like a hole from most angles, a boolean
// that returns the wrong set still produces a plausible solid.  Several bugs
// found while writing these were invisible on screen, and two were only visible
// because a *different* bug was fixed first.
//
//   cmake --build build && ./build/kernel_tests

#include "geometry.h"
#include "csg.h"
#include "sketch.h"
#include "cad_math.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

using namespace cad;

// ---------------------------------------------------------------- harness --
static int g_checks = 0;
static int g_failed = 0;
static const char* g_section = "";

static void section(const char* name) {
    g_section = name;
    std::printf("\n%s\n", name);
}
static void check(const char* what, bool ok, const char* detail = "") {
    g_checks++;
    if (!ok) g_failed++;
    std::printf("  %-52s %s%s%s\n", what, ok ? "ok" : "FAIL",
                detail[0] ? "  " : "", detail);
}
static void checkNear(const char* what, double got, double expect, double tol) {
    char detail[128];
    std::snprintf(detail, sizeof(detail), "got %.4f expect %.4f", got, expect);
    check(what, std::abs(got - expect) < tol, std::abs(got - expect) < tol ? "" : detail);
}

// ------------------------------------------------------------- geometry ----

// Signed volume via the divergence theorem.  Only equals the true volume when
// the surface is closed *and* consistently oriented outward, which makes it a
// sharp test for both.
static double volumeOf(const Solid& s) {
    Mesh m = s.tessellate();
    double v = 0;
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const Vec3& a = m.positions[m.indices[i]];
        const Vec3& b = m.positions[m.indices[i + 1]];
        const Vec3& c = m.positions[m.indices[i + 2]];
        v += (double)a.dot(b.cross(c)) / 6.0;
    }
    return v;
}

// Count mesh edges not shared by exactly two triangles.  Zero means watertight.
static int openEdges(const Solid& s) {
    Mesh m = s.tessellate();
    typedef std::array<long long, 3> Key;
    std::map<std::pair<Key, Key>, int> counts;
    auto key = [](const Vec3& p) -> Key {
        return Key{ (long long)std::llround(p.x * 1000),
                    (long long)std::llround(p.y * 1000),
                    (long long)std::llround(p.z * 1000) };
    };
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        Key k[3] = { key(m.positions[m.indices[i]]),
                     key(m.positions[m.indices[i + 1]]),
                     key(m.positions[m.indices[i + 2]]) };
        for (int e = 0; e < 3; e++) {
            Key a = k[e], b = k[(e + 1) % 3];
            if (a == b) continue;
            counts[{ std::min(a, b), std::max(a, b) }]++;
        }
    }
    int bad = 0;
    for (auto& kv : counts) if (kv.second != 2) bad++;
    return bad;
}

static std::vector<Vec3> circleXZ(float cx, float cz, float y, float r, int n) {
    std::vector<Vec3> v;
    for (int i = 0; i < n; i++) {
        float a = i * 2 * PI / n;
        v.push_back({ cx + std::cos(a) * r, y, cz + std::sin(a) * r });
    }
    return v;
}
static float ngonArea(int n, float r) { return 0.5f * n * r * r * std::sin(2 * PI / n); }

// ---------------------------------------------------------------- tests ----

static void testBooleans() {
    section("Booleans against analytic volumes");
    Solid box = makeBox({0,0,0}, {10,10,10});
    Solid cyl = extrude(circleXZ(0,0,-8, 3.0f, 24), {0,1,0}, 16.0f);
    double core = ngonArea(24, 3.0f) * 10.0;

    // Subtract used to return A intersect B: it omitted the first a.clipTo(b)
    // and inverted b in the wrong place.  It looked correct only because
    // extrude() was emitting inside-out solids, so two faults cancelled.
    checkNear("difference", volumeOf(csgSubtract(box, cyl)), 1000.0 - core, 1.0);
    checkNear("intersection", volumeOf(csgIntersect(box, cyl)), core, 1.0);
    checkNear("union", volumeOf(csgUnion(box, cyl)), 1000.0 + volumeOf(cyl) - core, 1.0);
}

static void testExtrudeOrientation() {
    section("Extrude is independent of profile winding");
    // Profiles built from cos/sin wind clockwise about +Y, so before loops were
    // normalised every extruded cylinder came out inside-out.
    std::vector<Vec3> ccw = {{-5,0,-5},{-5,0,5},{5,0,5},{5,0,-5}};
    std::vector<Vec3> cw(ccw.rbegin(), ccw.rend());
    checkNear("counter-clockwise profile", volumeOf(extrude(ccw, {0,1,0}, 5.0f)), 500.0, 0.01);
    checkNear("clockwise profile",         volumeOf(extrude(cw,  {0,1,0}, 5.0f)), 500.0, 0.01);
}

static void testHoles() {
    section("Faces with holes");
    std::vector<Vec3> sq = {{-5,0,-5},{5,0,-5},{5,0,5},{-5,0,5}};
    Solid s = extrudeWithHoles(sq, { circleXZ(0,0,0, 2.0f, 32) }, {0,1,0}, 4.0f);
    checkNear("bored slab volume", volumeOf(s), (100.0 - ngonArea(32, 2.0f)) * 4.0, 0.01);
    check("bored slab is watertight (valid STL)", openEdges(s) == 0);

    // Several holes in one face need bridging that does not defeat ear
    // clipping; five used to make the cap triangulate to nothing.
    for (int n = 1; n <= 5; n++) {
        std::vector<std::vector<Vec3>> holes;
        const float px[5] = {0, -3.2f, 3.2f, 3.2f, -3.2f};
        const float pz[5] = {0, -3.2f, -3.2f, 3.2f, 3.2f};
        for (int i = 0; i < n; i++)
            holes.push_back(circleXZ(px[i], pz[i], 0, i == 0 ? 1.5f : 0.7f, 20));
        Solid m = extrudeWithHoles(sq, holes, {0,1,0}, 3.0f);
        char label[64];
        std::snprintf(label, sizeof(label), "%d hole(s) in one face: watertight", n);
        check(label, openEdges(m) == 0);
    }

    // Push/pull a bored face repeatedly; the bore must survive every round.
    Solid chain = extrudeWithHoles(sq, { circleXZ(0,0,0, 2.0f, 24) }, {0,1,0}, 4.0f);
    double annulus = 100.0 - ngonArea(24, 2.0f);
    bool held = true;
    for (int step = 2; step <= 4 && held; step++) {
        uint32_t top = UINT32_MAX; float bestY = -1e9f;
        for (uint32_t i = 0; i < chain.faces.size(); i++)
            if (chain.faces[i].normal.y > 0.999f && chain.faces[i].centroid().y > bestY) {
                bestY = chain.faces[i].centroid().y; top = i;
            }
        if (top == UINT32_MAX) { held = false; break; }
        auto loops = chain.regionBoundaryLoops(chain.coplanarRegion(top));
        if (loops.size() < 2) { held = false; break; }   // hole was lost
        std::vector<std::vector<Vec3>> holes(loops.begin() + 1, loops.end());
        chain = extrudeWithHoles(loops[0], holes, {0,1,0}, (float)step);
        held = std::abs(volumeOf(chain) - annulus * step) < 0.01 && openEdges(chain) == 0;
    }
    check("bore survives repeated push/pull", held);
}

static void testManyHolesAndRepeatedCuts() {
    section("Scaling: many holes, and cut after cut");
    // A 4x4 grid of bores in a single face exercises the keyhole bridging far
    // harder than one hole does; the order holes are merged in matters.
    const float W = 80.0f, r = 3.0f; const int seg = 20;
    std::vector<Vec3> rect = {{-W/2,0,-W/2},{W/2,0,-W/2},{W/2,0,W/2},{-W/2,0,W/2}};
    for (int g : {2, 4}) {
        std::vector<std::vector<Vec3>> holes;
        for (int i = 0; i < g; i++)
            for (int j = 0; j < g; j++)
                holes.push_back(circleXZ(-W/2 + W*(i+1.0f)/(g+1),
                                         -W/2 + W*(j+1.0f)/(g+1), 0, r, seg));
        Solid s = extrudeWithHoles(rect, holes, {0,1,0}, 5.0f);
        double expect = (W*W - g*g*ngonArea(seg, r)) * 5.0;
        char label[80];
        std::snprintf(label, sizeof(label), "%d holes in one face: watertight", g*g);
        check(label, openEdges(s) == 0);
        std::snprintf(label, sizeof(label), "%d holes in one face: exact volume", g*g);
        checkNear(label, volumeOf(s), expect, 0.05);
    }

    // Face count must grow with the geometry, not with how many booleans have
    // been run.  Before fragments were stitched back together this compounded
    // and a handful of cuts produced thousands of faces.
    Solid acc = makeBox({0,0,0}, {40,20,40});
    size_t previous = acc.faces.size();
    bool linear = true;
    for (int i = 0; i < 4; i++) {
        acc = csgSubtract(acc, extrude(circleXZ(-12.0f + i*8.0f, 0, -15, 3.0f, 20), {0,1,0}, 30.0f));
        if (acc.faces.size() > previous + 40) linear = false;   // ~20 walls per bore
        previous = acc.faces.size();
    }
    check("four successive cuts keep the face count linear", linear);
    checkNear("volume exact after four cuts", volumeOf(acc),
              40.0*40.0*20.0 - 4*ngonArea(20, 3.0f)*20.0, 0.5);
}

static void testChamferFillet() {
    section("Chamfer and fillet remove the right amount");
    Solid box = makeBox({0,0,0}, {10,10,10});
    // These were out by 13x and 30x, and scaled linearly instead of
    // quadratically, because the new bevel/strip faces were wound inward.
    for (float d : {0.5f, 1.0f, 2.0f}) {
        Solid s = chamfer(box, {0u}, d);
        char label[64];
        std::snprintf(label, sizeof(label), "chamfer d=%.1f removes d^2/2 * length", d);
        checkNear(label, 1000.0 - volumeOf(s), (double)d * d / 2 * 10, 0.01);
        check("  chamfer stays watertight", openEdges(s) == 0);
    }
    for (float r : {0.5f, 1.0f, 2.0f}) {
        Solid s = fillet(box, {0u}, r);
        double ideal = (r * r - PI * r * r / 4.0) * 10.0;
        double got = 1000.0 - volumeOf(s);
        char label[64];
        std::snprintf(label, sizeof(label), "fillet r=%.1f within arc faceting error", r);
        // A 10-segment quarter arc under-fills the true circle by ~1.5%.
        check(label, got > ideal * 0.98 && got < ideal * 1.05);
        check("  fillet stays watertight", openEdges(s) == 0);
    }
}

static void testCurvesAndRims() {
    section("Curved edges behave as one edge");
    std::vector<Vec3> sq = {{-15,0,-15},{15,0,-15},{15,0,15},{-15,0,15}};
    Solid plate = extrudeWithHoles(sq, { circleXZ(0,0,0, 6.0f, 32) }, {0,1,0}, 8.0f);

    std::vector<int> surf = plate.smoothSurfaces();
    int surfaces = 0;
    for (int v : surf) surfaces = std::max(surfaces, v + 1);
    check("a tessellated bore counts as one smooth surface", surfaces == 7);

    // The rim between the top face and the bore wall.
    int rim = -1;
    for (uint32_t i = 0; i < plate.edges.size() && rim < 0; i++) {
        const auto& e = plate.edges[i];
        if (e.face0 == NULL_ID || e.face1 == NULL_ID) continue;
        Vec3 m = e.midpoint();
        if (std::abs(m.y - 8.0f) < 1e-3f && std::sqrt(m.x*m.x + m.z*m.z) < 7.0f) rim = (int)i;
    }
    check("a rim edge exists", rim >= 0);
    // One click has to take the whole ring; the old rule only recognised faces
    // tagged by fillet(), so an extruded circle was never grouped at all.
    std::vector<uint32_t> ring = plate.curveGroupEdges((uint32_t)rim);
    check("clicking one rim facet selects the whole circle", ring.size() == 32);
    Solid box = makeBox({0,0,0}, {10,10,10});
    check("a straight box edge is still just itself", box.curveGroupEdges(0).size() == 1);

    section("Chamfer and fillet on a hole rim");
    // Trimming a face back from an edge in the middle of it slices the face in
    // two; a rim has to be widened as a closed curve instead.
    double before = volumeOf(plate);
    Solid ch = chamfer(plate, ring, 1.0f);
    double perimeter = 2 * 32 * 6.0 * std::sin(PI / 32);
    check("chamfered rim stays watertight", openEdges(ch) == 0);
    checkNear("chamfer removes about perimeter * d^2/2",
              before - volumeOf(ch), perimeter * 0.5, 2.0);
    check("chamfer added a face per rim segment", ch.faces.size() == plate.faces.size() + 32);

    Solid fi = fillet(plate, ring, 1.0f);
    check("filleted rim stays watertight", openEdges(fi) == 0);
    check("filleted rim removes less than a chamfer would",
          (before - volumeOf(fi)) > 0 && (before - volumeOf(fi)) < (before - volumeOf(ch)));
}

static void testProfileOffset() {
    section("2D profile offset");
    std::vector<Vec2> circ;
    for (int i = 0; i < 32; i++) {
        float a = i * 2 * PI / 32;
        circ.push_back({ std::cos(a) * 6.0f, std::sin(a) * 6.0f });
    }
    auto area = [](const std::vector<Vec2>& L) {
        float a = 0;
        for (size_t i = 0; i < L.size(); i++) {
            const Vec2& p = L[i]; const Vec2& q = L[(i + 1) % L.size()];
            a += p.x * q.y - q.x * p.y;
        }
        return std::abs(a) * 0.5f;
    };
    // The mitre used a cross product where it needed a dot, so the step blew up
    // on any gentle corner, got clamped, and collapsed the profile: a 32-gon
    // offset outward came back with a twentieth of its area.
    float in  = area(offsetProfile2D(circ,  1.0f));
    float out = area(offsetProfile2D(circ, -1.0f));
    float lo = std::min(in, out), hi = std::max(in, out);
    checkNear("offset inward gives the r-1 ring", lo, ngonArea(32, 5.0f), 0.5);
    checkNear("offset outward gives the r+1 ring", hi, ngonArea(32, 7.0f), 0.5);
}

static void testSplit() {
    section("Split is exact away from the origin");
    // An inverted cap shifts the volume by twice its own contribution, and that
    // contribution is zero when the cut passes through the origin — which is
    // why splitting a centred cube hid this bug completely.
    Solid box = makeBox({0,0,0}, {20,10,10});
    auto pieces = split(box, Plane(Vec3{1,0,0}, Vec3{-2,0,0}));
    checkNear("piece on +normal side (x -2..10)", volumeOf(pieces.first), 1200.0, 0.01);
    checkNear("piece on -normal side (x -10..-2)", volumeOf(pieces.second), 800.0, 0.01);
    checkNear("pieces sum to the original", volumeOf(pieces.first) + volumeOf(pieces.second), 2000.0, 0.01);
    check("both pieces watertight", openEdges(pieces.first) == 0 && openEdges(pieces.second) == 0);

    // Volume is only translation-invariant for a closed, consistently wound
    // mesh, so this catches an inverted cap that the sum above would not.
    Solid moved = pieces.first;
    translateSolid(moved, {40, -15, 7});
    checkNear("volume unchanged by translation", volumeOf(moved), 1200.0, 0.01);
}

static void testMove() {
    section("Move preserves volume and carries bores");
    std::vector<Vec3> sq = {{-5,0,-5},{5,0,-5},{5,0,5},{-5,0,5}};
    Solid s = extrudeWithHoles(sq, { circleXZ(0,0,0, 2.0f, 24) }, {0,1,0}, 4.0f);
    double before = volumeOf(s);
    AABB b0 = s.bounds();
    translateSolid(s, {7, 3, -2});
    checkNear("volume unchanged", volumeOf(s), before, 0.001);
    checkNear("shifted by exactly the delta", (double)s.bounds().center().x, (double)b0.center().x + 7.0, 0.001);
    check("still watertight after the move", openEdges(s) == 0);
}

static void testFaceIdentity() {
    section("Booleans keep face identity");
    Solid box = makeBox({0,0,0}, {10,10,10});
    Solid cut = csgSubtract(box, extrude(circleXZ(0,0,-8, 3.0f, 24), {0,1,0}, 16.0f));
    // A cube with one through-bore is 6 outer faces (two of them holed) plus
    // one wall per bore segment.  Before fragments were stitched back together
    // this came out at 93 faces and the top could not be selected as one face.
    check("face count is the ideal 6 + 24, not a sliver soup",
          cut.faces.size() <= 34);
    int holed = 0;
    for (auto& f : cut.faces) if (!f.innerLoops.empty()) holed++;
    check("two faces carry the bore as a hole", holed == 2);
    checkNear("volume still exact", volumeOf(cut), 1000.0 - ngonArea(24, 3.0f) * 10.0, 1.0);

    // Whatever fragmentation remains must still select as one logical face.
    uint32_t top = UINT32_MAX;
    for (uint32_t i = 0; i < cut.faces.size(); i++)
        if (cut.faces[i].normal.y > 0.999f) { top = i; break; }
    check("a top fragment resolves to a coplanar region",
          top != UINT32_MAX && !cut.coplanarRegion(top).empty());
}

static void testEdgeWelding() {
    section("Edge welding has no coordinate range limit");
    // The old vertex key packed three coordinates into one integer in base
    // 1,000,001, which is only injective inside a +/-50 mm box.
    for (float c : {0.0f, 60.0f, 250.0f, -400.0f}) {
        Solid b = makeBox({c, c, c}, {20, 20, 20});
        int twoFace = 0;
        for (auto& e : b.edges) if (e.face0 != NULL_ID && e.face1 != NULL_ID) twoFace++;
        char label[64];
        std::snprintf(label, sizeof(label), "box centred at %+.0f mm welds to 12 edges", c);
        check(label, b.edges.size() == 12 && twoFace == 12);
    }
}

static void testWireframe() {
    section("Wireframe shows feature edges, not artefacts");
    Solid box = makeBox({0,0,0}, {10,10,10});
    Solid cut = csgSubtract(box, extrude(circleXZ(0,0,-8, 3.0f, 24), {0,1,0}, 16.0f));
    size_t segs = cut.wireframe().points.size() / 2;
    // 12 box edges plus the two bore rims; smooth facet seams and stray
    // half-edges must not appear.
    check("edge count is close to 12 + 2 rims", segs > 20 && segs < 80);
}

static void testSnapping() {
    section("Sketch snapping");
    Sketch sk;
    sk.setup(Plane(Vec3{0,1,0}, Vec3{0,5,0}), Vec3{0,5,0});
    sk.snapToGrid = false;
    sk.snapDistance = 0.6f;
    sk.setReferenceLoops({ { {-5,5,-5},{5,5,-5},{5,5,5},{-5,5,5} } });
    check("the face being drawn on is magnetic", sk.referenceLoops.size() == 1);

    Vec2 c0 = sk.referenceLoops[0][0], c1 = sk.referenceLoops[0][1];
    // Nearest-point alone can never select a corner: the perpendicular distance
    // to an edge is always shorter than the distance to that edge's endpoint.
    SnapResult corner = sk.findSnap(c0 + Vec2{0.3f, 0.3f});
    check("a corner beats the edge it sits on", corner.type == SnapType::Endpoint);
    SnapResult mid = sk.findSnap((c0 + c1) * 0.5f + Vec2{0.2f, 0.2f});
    check("an edge midpoint is found", mid.type == SnapType::Midpoint);
    SnapResult none = sk.findSnap(Vec2{2.5f, 2.5f});
    check("nothing snaps when out of range", !none.valid());

    sk.snapToGrid = true; sk.gridSize = 1.0f;
    SnapResult stillCorner = sk.findSnap(c0 + Vec2{0.3f, 0.3f});
    check("real geometry still beats the grid", stillCorner.type == SnapType::Endpoint);
}

static void testCameraBasis() {
    section("View matrix survives looking straight down");
    // Sketching on the top plane drives pitch to exactly 90 degrees, where the
    // view direction is parallel to world up.  lookAt's cross product is then
    // zero and the whole viewport used to go blank.
    Vec3 eye{0, 30, 0}, target{0, 0, 0};
    Mat4 m = Mat4::lookAt(eye, target, {0, 1, 0});
    bool finite = true, nonZero = false;
    for (int i = 0; i < 16; i++) {
        if (!std::isfinite(m.m[i])) finite = false;
        if (std::abs(m.m[i]) > 1e-6f) nonZero = true;
    }
    check("lookAt straight down is finite and non-degenerate", finite && nonZero);
}

int main() {
    std::printf("SimpleCad kernel tests\n======================\n");
    testBooleans();
    testExtrudeOrientation();
    testHoles();
    testManyHolesAndRepeatedCuts();
    testChamferFillet();
    testCurvesAndRims();
    testProfileOffset();
    testSplit();
    testMove();
    testFaceIdentity();
    testEdgeWelding();
    testWireframe();
    testSnapping();
    testCameraBasis();

    std::printf("\n======================\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
