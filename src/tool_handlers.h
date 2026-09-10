#pragma once
#include "app_state.h"

// ---- Tool-specific handlers (organized separately) ----

// Extrude tool
void handleExtrudeDrag(float dy);
void handleExtrudeRelease();
void applyExtrude();

// Chamfer/Fillet tools
void applyActiveEdgeModifier();
void clearActiveEdgeModifierSelection();
void updateEdgeModifierPreview();

// Sketch tools (already in input.cpp, kept there for now)
void enterSketchOnPlane(cad::Plane plane, cad::Vec3 origin, int sourceSolidIdx = -1, int sourceFaceIdx = -1, int sourcePlaneIdx = -1);
void startSketchTool(Tool tool);
void handleSketchClick(cad::Vec2 sketchPos);
