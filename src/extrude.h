#pragma once
#include "app_state.h"

// Extrude mode lifecycle
void enterExtrudeMode();
void updateExtrudePreview();
void performExtrude();

// Extrude arrow drag
void handleExtrudeDrag(float dy);
void handleExtrudeRelease();
bool setExtrudeDistanceFromFace(int solidIndex, int faceIndex);
