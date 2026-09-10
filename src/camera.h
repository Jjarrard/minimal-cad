#pragma once
#include "app_state.h"

// Camera animation
void animateCameraToPlane(cad::Plane plane, cad::Vec3 center);
void animateCameraTo(float yaw, float pitch, cad::Vec3 target);
void updateCameraAnimation(float dt);
