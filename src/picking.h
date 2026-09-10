#pragma once
#include "app_state.h"

// Ray-face intersection test (point-in-polygon)
bool rayIntersectFace(const cad::Ray& ray, const cad::BRepFace& face, float& outT);

// Update hovered solid/face/edge indices from mouse position
void updatePicking();

// Index of the construction-plane quad under the cursor, or -1.
int pickConstructionPlane(const cad::Ray& ray, float* outT = nullptr);
