#pragma once
#include "cad_math.h"
#include <vector>
#include <string>

namespace cad {

// ---- Sketch Entity Types ----
enum class SketchEntityType {
    Line,
    Circle,
    Rectangle,
    Arc,
};

// ---- Sketch Entity ----
struct SketchEntity {
    SketchEntityType type;
    // Line: p0→p1
    // Circle: p0=center, radius
    // Rectangle: p0=corner1, p1=corner2
    // Arc: p0=center, p1=start, p2=end, radius
    Vec2 p0, p1, p2;
    float radius = 0;
    bool selected = false;
    bool construction = false; // construction line (not part of profile)
};

// ---- Snap Target ----
enum class SnapType {
    None,
    Endpoint,
    Midpoint,
    Center,
    Grid,
    Perpendicular,
    Horizontal,
    Vertical,
    Intersection,
    OnLine,
    Parallel,
    Origin,
};

struct SnapResult {
    SnapType type = SnapType::None;
    Vec2 point;
    float distance = 1e18f;
    Vec2 refLineDir;  // direction of the reference line (for parallel/perp symbols)
    bool valid() const { return type != SnapType::None; }
};

// ---- Sketch ----
class Sketch {
public:
    // The plane this sketch lives on
    Plane sketchPlane;
    Vec3 origin;
    Vec3 axisU, axisV; // local 2D axes on the plane

    // Sketch entities
    std::vector<SketchEntity> entities;

    // Geometry that is not part of the sketch but is still magnetic — normally
    // the boundary of the face the sketch was created on, plus anything else
    // lying in the sketch plane.  Held in sketch 2D coordinates.  Without this
    // a fresh sketch on a face has nothing to snap to but the grid and origin.
    std::vector<std::vector<Vec2>> referenceLoops;

    // Grid
    float gridSize = 1.0f;
    bool snapToGrid = true;
    float snapDistance = 0.15f; // in sketch units

    // State
    bool active = false;

    // Setup sketch on a plane
    void setup(Plane plane, Vec3 origin);

    // Convert between 3D world and 2D sketch coords
    Vec2 worldToSketch(Vec3 worldPos) const;
    Vec3 sketchToWorld(Vec2 sketchPos) const;

    // Snapping
    SnapResult findSnap(Vec2 cursor, bool hasFrom = false, Vec2 fromPoint = {0,0}) const;

    // Project 3D loops onto the sketch plane and keep them as snap references.
    void setReferenceLoops(const std::vector<std::vector<Vec3>>& loops3D);

    // Add entities
    void addLine(Vec2 a, Vec2 b);
    void addCircle(Vec2 center, float radius);
    void addRectangle(Vec2 corner1, Vec2 corner2);
    void addArc(Vec2 center, Vec2 start, Vec2 end, float radius);

    // Extract closed profiles (for extrude)
    // Returns list of closed polylines in 3D
    std::vector<std::vector<Vec3>> extractProfiles() const;

    // Extract closed profiles as 2D sketch coords
    std::vector<std::vector<Vec2>> extractProfiles2D() const;

    // Trim: delete the segment of an entity nearest to a click point
    bool trimAtPoint(Vec2 point, float tolerance);

    // Offset all closed profiles by distance
    void offsetProfiles(float distance);

    // Delete selected entities
    void deleteSelected();

    // Select entity near point (returns index or -1)
    int pickEntity(Vec2 point, float tolerance) const;

    // Clear
    void clear();
};

} // namespace cad
