#pragma once
#include "cad_math.h"
#include "geometry.h"
#include "sketch.h"
#include "renderer.h"

#include <vector>
#include <string>
#include <cstring>
#include <set>
#include <cfloat>

using namespace cad;

// ---- Tools ----
enum class Tool {
    Select,
    SketchLine,
    SketchCircle,
    SketchRect,
    SketchTrim,
    SketchOffset,
    Extrude,
    Move,
    Chamfer,
    Fillet,
    Split,
    Measure,
    ConstructionPlane,
    Join,
};

inline const char* toolName(Tool t) {
    switch (t) {
        case Tool::Select:        return "Select";
        case Tool::SketchLine:    return "Line";
        case Tool::SketchCircle:  return "Circle";
        case Tool::SketchRect:    return "Rectangle";
        case Tool::SketchTrim:    return "Trim";
        case Tool::SketchOffset:  return "Offset";
        case Tool::Extrude:       return "Extrude";
        case Tool::Move:          return "Move";
        case Tool::Chamfer:       return "Chamfer";
        case Tool::Fillet:        return "Fillet";
        case Tool::Split:         return "Split";
        case Tool::Measure:       return "Measure";
        case Tool::ConstructionPlane: return "Plane";
        case Tool::Join:          return "Join";
    }
    return "?";
}

// ---- Extrude mode/direction ----
enum class ExtrudeDirection { OneSide, Symmetric, TwoSides };
enum class ExtrudeBoolOp { NewBody, Join, Cut, Intersect };

// ---- Plane picker state ----
enum class PlanePickMode {
    None,
    WaitingForPlane
};

// ---- Dimension input ----
struct DimInput {
    bool active = false;
    bool focusLength = false;
    bool focusAngle = false;
    char lengthBuf[32] = "";
    char angleBuf[32] = "";
    float length = 0;
    float angle = 0;
    bool lengthSet = false;
    bool angleSet = false;
    float liveLength = 0;
    float liveAngle = 0;

    void reset() {
        active = false;
        focusLength = false;
        focusAngle = false;
        lengthBuf[0] = '\0';
        angleBuf[0] = '\0';
        length = 0;
        angle = 0;
        lengthSet = false;
        angleSet = false;
        liveLength = 0;
        liveAngle = 0;
    }

    void activate() {
        active = true;
        focusLength = true;
        lengthBuf[0] = '\0';
        angleBuf[0] = '\0';
        lengthSet = false;
        angleSet = false;
        liveLength = 0;
        liveAngle = 0;
    }
};

// ---- Construction Plane ----
// A plane keeps the place it was born (baseOrigin + normal) and a signed
// offset along that normal, rather than only a baked world plane.  That is what
// lets it be slid after creation without losing what it was attached to.
struct ConstructionPlaneData {
    cad::Plane  plane;            // normal + distance (world-space), derived
    cad::Vec3   origin;           // centre of the drawn quad, derived
    cad::Vec3   baseOrigin;       // where it was created, before any offset
    cad::Vec3   normal{0, 1, 0};  // the axis it slides along
    float       offset = 0.0f;    // signed distance from baseOrigin
    float       extent = 15.0f;   // half-size of the visible quad (mm)
    std::string name;
    bool        visible = true;

    // Recompute the world plane from baseOrigin + normal * offset.
    void refresh() {
        origin = baseOrigin + normal * offset;
        plane  = cad::Plane(normal, origin);
    }
};

// ---- Application state ----
struct AppState {
    std::vector<Solid> solids;
    Sketch sketch;
    Camera camera;
    Renderer renderer;

    Tool currentTool = Tool::Select;
    Tool pendingTool = Tool::Select;
    bool inSketchMode = false;

    PlanePickMode planePick = PlanePickMode::None;

    // Track which face the sketch was created on (for profile splitting)
    int sketchSourceSolidIdx = -1;
    int sketchSourceFaceIdx = -1;
    // Index into constructionPlanes if the active sketch was created on a
    // construction plane (rather than a face).  Used to exit sketch cleanly
    // when that plane is deleted.
    int sketchSourcePlaneIdx = -1;
    std::vector<Vec3> sketchSourceFaceLoop; // the source face's outer loop in 3D

    bool sketchDrawing = false;
    Vec2 sketchDrawStart;
    Vec2 sketchDrawCurrent;
    SnapResult currentSnap;

    DimInput dimInput;

    int hoveredSolidIdx = -1;
    int hoveredFaceIdx = -1;
    // Edge hover is tracked with its own solid index.  Sharing hoveredSolidIdx
    // let a nearby edge repoint it at another body while hoveredFaceIdx still
    // referred to the previous one, so the highlight jumped to an unrelated
    // face — very visible on thin faces, where the cursor is always within the
    // edge threshold.
    int hoveredEdgeSolidIdx = -1;
    int hoveredEdgeIdx = -1;
    // The whole logical curve under the cursor (a bore rim is many segments).
    // Cached: the walk labels every face, so it is far too slow to redo per frame.
    std::vector<uint32_t> hoveredEdgeGroup;
    int hoveredEdgeGroupSolid = -1;
    int hoveredEdgeGroupEdge = -1;
    int hoveredCornerSolidIdx = -1;
    Vec3 hoveredCornerPos{0,0,0};
    bool hoveredCornerValid = false;
    int selectedSolidIdx = -1;
    int selectedFaceIdx = -1;
    int selectedEdgeIdx = -1;

    // A boolean shatters one flat face into many fragments, so the face under
    // the cursor is expanded to its whole coplanar region for highlighting and
    // selection.  Cached because the walk is O(faces) and hover runs per frame.
    std::vector<uint32_t> hoveredFaceRegion;
    int hoveredRegionSolid = -1;
    int hoveredRegionFace = -1;

    float extrudeDistance = 10.0f;
    float extrudeDistance2 = 10.0f; // for TwoSides: other-side distance
    ExtrudeDirection extrudeDir = ExtrudeDirection::OneSide;
    ExtrudeBoolOp extrudeBoolOp = ExtrudeBoolOp::NewBody;
    bool extrudeBoolManual = false; // user manually chose the bool operation
    bool extrudePreviewing = false;
    std::vector<std::vector<Vec3>> extrudeProfiles; // cached for preview
    std::vector<Solid> extrudePreviewSolids; // preview geometry
    int extrudeTargetSolidIdx = -1; // solid for boolean ops
    int extrudeSelectedProfile = -1; // which profile region is selected (-1 = all/none)

    // Extrude arrow drag state
    bool extrudeDragging = false;
    Vec3 extrudeArrowBase{0,0,0};
    Vec3 extrudeArrowDir{0,1,0};
    int extrudeFaceSolidIdx = -1;
    int extrudeFaceIdx = -1;
    float extrudeDragStartX = 0;
    float extrudeDragStartY = 0;
    float extrudeDragStartDist = 0;
    std::vector<Vec3> extrudeFaceProfile; // the face loop being extruded
    // Hole loops of the face region being push/pulled, so extruding an annular
    // face keeps its bore instead of filling it.
    std::vector<std::vector<Vec3>> extrudeFaceHoles;

    // Extrude distance keyboard input
    bool extrudeInputActive = false;
    bool extrudeInputFocus = false;
    char extrudeInputBuf[32] = "";

    float chamferDistance = 1.0f;
    float filletRadius = 1.0f;
    int edgeModifierTargetSolidIdx = -1;
    bool edgeModifierDragging = false;
    float edgeModifierDragStartX = 0;
    float edgeModifierDragStartY = 0;
    float edgeModifierDragStartValue = 0;
    bool edgeModifierGizmoVisible = false;
    Vec3 edgeModifierGizmoBase{0,0,0};
    Vec3 edgeModifierGizmoDir{0,1,0};
    Vec2 edgeModifierGizmoBaseScreen{0,0};
    Vec2 edgeModifierGizmoTipScreen{0,0};
    bool edgeModifierChipVisible = false;
    float edgeModifierChipX0 = 0;
    float edgeModifierChipY0 = 0;
    float edgeModifierChipX1 = 0;
    float edgeModifierChipY1 = 0;
    bool edgeModifierInputActive = false;
    bool edgeModifierInputFocus = false;
    char edgeModifierInputBuf[32] = "";
    
    // Live preview for chamfer/fillet
    bool edgeModifierPreviewing = false;
    Solid edgeModifierBackupSolid;
    Solid edgeModifierPreviewSolid;
    float edgeModifierLastPreviewAmount = -FLT_MAX;  // sentinel: no valid cache
    bool edgeModifierGizmoCached = false;  // True when gizmo is stable, don't recalc while dragging
    
    // Offset sketch
    float offsetDistance = 1.0f;

    // ---- Move tool: 3-axis translate gizmo ----
    // axis 0/1/2 = X/Y/Z; -1 = nothing grabbed.
    int   moveAxis = -1;          // axis currently being dragged
    int   moveHoverAxis = -1;     // axis under the cursor
    bool  moveDragging = false;
    Vec3  moveGizmoOrigin{0, 0, 0};
    Vec3  moveStartCentroid{0, 0, 0};
    float moveDragStartValue = 0; // projection of the cursor at drag start
    Vec3  moveAccumulated{0, 0, 0};
    char  moveInputBuf[3][32] = {"", "", ""};

    // ---- Construction plane offset drag ----
    bool  planeDragging = false;
    bool  planeHandleHovered = false;
    float planeDragStartOffset = 0;
    float planeDragStartValue = 0;
    char  planeOffsetBuf[32] = "";

    // Multi-select
    std::set<int> selectedSolidSet;
    std::set<int> selectedFaceSet;
    std::set<int> selectedEdgeSet;

    int hoveredConstructionPlaneIdx = -1;  // plane quad under the cursor
    int hoveredOriginPlane = -1; // 0=XY(Top), 1=XZ(Front), 2=YZ(Side)

    bool measureHasFirst = false;
    Vec3 measurePointA, measurePointB;

    int windowWidth = 1280, windowHeight = 800; // logical (ImGui) size

    float mouseX = 0, mouseY = 0;
    float lastMouseX = 0, lastMouseY = 0;
    bool mouseMiddle = false;
    bool mouseLeft = false;
    bool mouseRight = false;
    bool shiftDown = false;
    bool altDown = false;
    // True while a camera orbit/pan drag is in progress, so click handlers and
    // the context menu can stand down.
    bool cameraNavigating = false;
    // Right-drag orbits the camera, so the context menu must open on *release*
    // and only when the button didn't travel.  Without this the menu pops open
    // every time you start an orbit.
    float rightDownX = 0, rightDownY = 0;
    bool  rightDragMoved = false;

    // Marquee (drag-rectangle) selection.  When the user presses left mouse
    // over empty viewport in Select / Chamfer / Fillet, we record the start
    // position and watch for a drag.  If the cursor moves more than a small
    // threshold before release, marqueeActive becomes true and the click
    // dispatcher is suppressed; on release we add everything inside the rect
    // to the appropriate selection set.
    bool marqueePending = false;
    bool marqueeActive = false;
    float marqueeStartX = 0;
    float marqueeStartY = 0;

    bool cameraAnimating = false;
    float cameraAnimT = 0;
    float cameraTargetYaw = 0, cameraTargetPitch = 0;
    float cameraStartYaw = 0, cameraStartPitch = 0;
    Vec3 cameraTargetPos{0,0,0};
    Vec3 cameraStartPos{0,0,0};

    std::string statusText = "Ready";

    bool showShortcutOverlay = false;
    // Side panels are overlays on a full-window viewport, so hiding them is
    // free screen space — worth a key on a 1280-wide laptop where they cost
    // a third of the width.
    bool panelsVisible = true;
    char projectPathBuf[512] = "project.scad";
    bool showExportStlDialog = false;
    bool focusExportStlPath = false;
    char exportStlPathBuf[512] = "export.stl";

    // Sketch visibility (persists after finishing sketch)
    bool sketchVisible = true;
    bool showSolidEdges = true;

    // Cached profile extraction for region-pick hover (avoid per-frame recompute)
    size_t cachedSketchEntityCount = 0;
    std::vector<std::vector<Vec3>> cachedProfiles3D;
    std::vector<std::vector<Vec2>> cachedProfiles2D;

    // Body tree — renamingSolidIdx >= 0 while a name is being edited in place.
    int  renamingSolidIdx = -1;
    bool renameFocusPending = false;
    char renameBuf[64] = "";
    std::vector<std::string> solidNames;
    std::vector<bool> solidVisible;
    std::vector<Color> solidColors; // per-solid appearance

    // Construction planes
    std::vector<ConstructionPlaneData> constructionPlanes;
    // Monotonic so names stay unique after a plane is deleted.
    int nextPlaneNumber = 1;
    int selectedConstructionPlaneIdx = -1;
};

// Global app state — defined in main.cpp
extern AppState app;
