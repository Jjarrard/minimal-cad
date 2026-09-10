#include "camera.h"

#include <cmath>
#include <algorithm>

using namespace cad;

void animateCameraToPlane(Plane plane, Vec3 center) {
    app.cameraAnimating = true;
    app.cameraAnimT = 0;
    app.cameraStartYaw = app.camera.yaw;
    app.cameraStartPitch = app.camera.pitch;
    app.cameraStartPos = app.camera.target;
    app.cameraTargetPos = center;

    Vec3 n = plane.normal;
    app.cameraTargetYaw = std::atan2(n.z, n.x) * RAD2DEG;
    app.cameraTargetPitch = std::asin(std::max(-1.0f, std::min(1.0f, n.y))) * RAD2DEG;

    while (app.cameraTargetYaw - app.cameraStartYaw > 180.0f) app.cameraTargetYaw -= 360.0f;
    while (app.cameraTargetYaw - app.cameraStartYaw < -180.0f) app.cameraTargetYaw += 360.0f;
}

void animateCameraTo(float yaw, float pitch, Vec3 target) {
    app.cameraAnimating = true;
    app.cameraAnimT = 0;
    app.cameraStartYaw = app.camera.yaw;
    app.cameraStartPitch = app.camera.pitch;
    app.cameraStartPos = app.camera.target;
    app.cameraTargetYaw = yaw;
    app.cameraTargetPitch = pitch;
    app.cameraTargetPos = target;

    while (app.cameraTargetYaw - app.cameraStartYaw > 180.0f) app.cameraTargetYaw -= 360.0f;
    while (app.cameraTargetYaw - app.cameraStartYaw < -180.0f) app.cameraTargetYaw += 360.0f;
}

void updateCameraAnimation(float dt) {
    if (!app.cameraAnimating) return;
    app.cameraAnimT += dt * 3.0f;
    if (app.cameraAnimT >= 1.0f) {
        app.cameraAnimT = 1.0f;
        app.cameraAnimating = false;
    }
    float t = app.cameraAnimT;
    t = t * t * (3.0f - 2.0f * t); // smoothstep
    app.camera.yaw = lerp(app.cameraStartYaw, app.cameraTargetYaw, t);
    app.camera.pitch = lerp(app.cameraStartPitch, app.cameraTargetPitch, t);
    app.camera.target = cad::lerp(app.cameraStartPos, app.cameraTargetPos, t);
}
