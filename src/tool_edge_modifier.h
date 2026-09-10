#pragma once
#include "app_state.h"

// Chamfer / Fillet edge-modifier tool helpers
// These are called from input.cpp (mouse/key dispatch) and scene.cpp (drag update).

void applyActiveEdgeModifier();
void clearActiveEdgeModifierSelection();
void updateEdgeModifierPreview();

// Returns true and starts a drag if the mouse is over the gizmo arrow.
bool tryStartEdgeModifierDragFromGizmo();

// Returns true and starts a drag if the mouse is close to a selected edge.
bool tryStartEdgeModifierDragNearSelectedEdges();

// Returns true if the mouse is inside the value chip.
bool isMouseInEdgeModifierChip();
