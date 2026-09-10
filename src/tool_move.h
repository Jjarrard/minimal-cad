#pragma once
#include "app_state.h"

// ---- Move tool: drag selected bodies along X, Y or Z ----

// Recompute where the gizmo sits (centroid of the current selection).
void updateMoveGizmo();

// Screen-space gizmo: draws the three axis handles and records which one is
// under the cursor.  Call once per frame while the Move tool is active.
void drawMoveGizmo();

// Returns true if the click was consumed by grabbing an axis handle.
bool handleMoveMouseDown();

// Called while dragging; translates the selection along the grabbed axis.
void handleMoveDrag();
void handleMoveRelease();

// Nudge the selection by an exact amount (from the properties panel).
void moveSelectionBy(cad::Vec3 delta, const char* undoLabel);

// ---- Construction plane: slide along its own normal ----
// Draws the offset handle for the selected plane and records hover state.
void drawPlaneHandle();
bool handlePlaneMouseDown();
void handlePlaneDrag();
void handlePlaneRelease();
