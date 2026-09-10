#pragma once
#include "cad_math.h"
#include "geometry.h"
#include "sketch.h"

#include <unordered_map>

#ifdef __APPLE__
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <OpenGL/gl3.h>
#else
// On Linux/Windows we need a loader — for now use the system GL
#include <GL/gl.h>
#endif

namespace cad {

// ---- Orbit Camera ----
class Camera {
public:
    Vec3 target{0, 0, 0};
    float distance = 10.0f;
    float yaw = 45.0f;   // degrees
    float pitch = 35.0f;  // degrees
    float fov = 45.0f;    // degrees
    float nearPlane = 0.05f;
    float farPlane = 500.0f;

    int viewportWidth = 800;
    int viewportHeight = 600;

    void orbit(float dx, float dy) {
        yaw += dx * 0.3f;
        pitch += dy * 0.3f;
        pitch = std::max(-89.0f, std::min(89.0f, pitch));
    }

    void pan(float dx, float dy) {
        float scale = distance * 0.002f;
        Vec3 right = getRightDir();
        Vec3 up = getUpDir();
        target = target + right * dx * scale - up * dy * scale;
    }

    // Exponential zoom.  `delta` is in notches: a mouse wheel click is ~1.0,
    // a trackpad two-finger scroll arrives as a stream of much smaller values.
    // Using exp() keeps the *proportional* step constant, so zooming feels the
    // same at every scale, and clamping the per-event delta stops macOS
    // momentum scrolling from teleporting the camera.
    void zoom(float delta) {
        float d = std::max(-3.0f, std::min(3.0f, delta));
        distance *= std::exp(-d * 0.12f);
        distance = std::max(0.1f, std::min(500.0f, distance));
    }

    // Zoom while keeping the point under the cursor pinned in place.  This is
    // what stops zooming from being a two-handed operation: without it you
    // zoom to the centre of the screen and then have to pan back to whatever
    // you were actually looking at.
    void zoomAt(float delta, float screenX, float screenY) {
        Ray before = screenToRay(screenX, screenY);
        float startDist = distance;
        zoom(delta);
        float applied = distance / startDist;   // <1 when zooming in
        if (std::abs(applied - 1.0f) < 1e-6f) return;

        // Pick the depth to pin: the point on the view axis at the old
        // distance, expressed along the cursor ray.
        Vec3 forward = getForwardDir();
        float denom = before.dir.dot(forward);
        if (std::abs(denom) < 1e-4f) return;
        float t = startDist / denom;
        Vec3 pinned = before.at(t);

        // Move the target so `pinned` keeps the same screen position.
        target = pinned + (target - pinned) * applied;
    }

    void fitTo(AABB bounds) {
        target = bounds.center();
        // Distance needed to just contain the bounding sphere in the vertical
        // field of view, plus a small margin.  The old flat 1.5x diagonal was
        // roughly 15% further out than required and left the model small in
        // the frame; deriving it from the fov keeps the fit tight at any lens.
        float radius = bounds.diagonal() * 0.5f;
        float halfFov = fov * DEG2RAD * 0.5f;
        float aspect = (float)viewportWidth / std::max(1, viewportHeight);
        if (aspect < 1.0f) halfFov = std::atan(std::tan(halfFov) * aspect);
        distance = (radius / std::max(0.1f, std::sin(halfFov))) * 1.05f;
        if (distance < 1.0f) distance = 10.0f;
    }

    Vec3 getEyePos() const {
        float yr = yaw * DEG2RAD, pr = pitch * DEG2RAD;
        return target + Vec3{
            std::cos(pr) * std::cos(yr) * distance,
            std::sin(pr) * distance,
            std::cos(pr) * std::sin(yr) * distance
        };
    }

    Vec3 getRightDir() const {
        float yr = yaw * DEG2RAD;
        return Vec3{std::sin(yr), 0, -std::cos(yr)}.normalized();
    }

    Vec3 getUpDir() const {
        return getRightDir().cross(getForwardDir()).normalized();
    }

    Vec3 getForwardDir() const {
        return (target - getEyePos()).normalized();
    }

    // Built from the same right/up/forward basis that screenToRay uses, rather
    // than from a hardcoded world up.  Sketching on the top plane drives pitch
    // to exactly ±90°, where the view direction is parallel to world up and a
    // lookAt() degenerates to a zero matrix — which blanked the whole viewport.
    // getRightDir is perpendicular to forward at every pitch, so this basis is
    // always well formed, and rendering can never disagree with picking.
    Mat4 viewMatrix() const {
        Vec3 eye = getEyePos();
        Vec3 f = getForwardDir();
        Vec3 s = getRightDir();
        Vec3 u = s.cross(f);

        Mat4 r;
        r.m[0] = s.x;  r.m[4] = s.y;  r.m[8]  = s.z;
        r.m[1] = u.x;  r.m[5] = u.y;  r.m[9]  = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -s.dot(eye);
        r.m[13] = -u.dot(eye);
        r.m[14] = f.dot(eye);
        return r;
    }

    Mat4 projMatrix() const {
        float aspect = (float)viewportWidth / std::max(1, viewportHeight);
        float dynamicNear = std::max(0.05f, distance * 0.01f);
        float dynamicFar = std::max(200.0f, distance * 20.0f);
        return Mat4::perspective(fov * DEG2RAD, aspect, dynamicNear, dynamicFar);
    }

    // Unproject screen coords to a ray
    Ray screenToRay(float screenX, float screenY) const {
        // Normalized device coords
        float nx = (2.0f * screenX / viewportWidth) - 1.0f;
        float ny = 1.0f - (2.0f * screenY / viewportHeight);

        float aspect = (float)viewportWidth / std::max(1, viewportHeight);
        float tanHalf = std::tan(fov * DEG2RAD * 0.5f);

        Vec3 eye = getEyePos();
        Vec3 forward = getForwardDir();
        Vec3 right = getRightDir();
        Vec3 up = getUpDir();

        Vec3 dir = (forward + right * (nx * tanHalf * aspect) + up * (ny * tanHalf)).normalized();
        return {eye, dir};
    }
};

// ---- GPU Mesh (VAO + VBO) ----
struct GPUMesh {
    GLuint vao = 0, vbo = 0, ebo = 0;
    uint32_t indexCount = 0;

    void upload(const Mesh& mesh);
    void uploadLines(const std::vector<Vec3>& points);
    void draw(GLenum mode = GL_TRIANGLES) const;
    void destroy();
};

// ---- Shader ----
struct Shader {
    GLuint program = 0;

    bool compile(const char* vertSrc, const char* fragSrc);
    void use() const;
    void setMat4(const char* name, const Mat4& m) const;
    void setVec3(const char* name, Vec3 v) const;
    void setVec4(const char* name, float x, float y, float z, float w) const;
    void setFloat(const char* name, float v) const;
    void destroy();
};

// ---- Renderer ----
class Renderer {
public:
    bool init();
    void shutdown();

    void beginFrame(int width, int height, float r, float g, float b);
    void endFrame();

    // Paints a vertical gradient behind the scene.
    void drawBackgroundGradient(Color top, Color bottom);

    // Draw a solid body
    void drawSolid(const Solid& solid, const Camera& cam, Color color, bool wireframe = false);

    // Draw wireframe edges
    void drawEdges(const Solid& solid, const Camera& cam, Color color, float lineWidth = 1.5f);

    // Draw the infinite grid on a plane
    void drawGrid(const Camera& cam, float gridSize, float extent);

    // Draw sketch entities
    void drawSketch(const Sketch& sketch, const Camera& cam);

    // Draw a single line in 3D
    void drawLine3D(Vec3 a, Vec3 b, const Camera& cam, Color color, float lineWidth = 1.0f);

    // Draw snap indicator
    void drawSnapIndicator(Vec3 pos, const Camera& cam, SnapType type);

    // Draw Fusion-style constraint symbol near cursor
    void drawConstraintSymbol(Vec3 pos, const Camera& cam, SnapType type, Vec3 refDir = {0,0,0});

    // Highlight a face
    void drawFaceHighlight(const BRepFace& face, const Camera& cam, Color color);

    // Highlight an edge
    void drawEdgeHighlight(Vec3 a, Vec3 b, const Camera& cam, Color color, float lineWidth = 3.0f);

    // Draw origin cube (3 faces at origin for plane selection)
    void drawOriginCube(const Camera& cam, float size, int hoveredFace);

    // Fill closed profiles with semi-transparent highlight
    void drawProfileFill(const std::vector<Vec3>& profile, const Camera& cam, Color color);

    // Draw extrude preview (transparent solid)
    void drawExtrudePreview(const Solid& solid, const Camera& cam, Color color);

private:
    Shader solidShader;
    Shader lineShader;
    Shader gridShader;
    Shader bgShader;
    GPUMesh tempMesh;   // reused for dynamic geometry
    GPUMesh tempLines;  // reused for line drawing
    GPUMesh gridMesh;       // minor lines
    GPUMesh gridMajorMesh;  // every 10th line
    bool gridBuilt = false;
    float gridStepBuilt = -1.0f;

    void buildGrid(float size, float extent);

    // Tessellation is ear-clipping plus a normal-smoothing pass, and drawSolid
    // runs every frame — so cache the result.  The key is a hash of the solid's
    // own vertex data rather than a dirty flag, which means no mutation path
    // can ever leave a stale mesh on screen.
    std::unordered_map<uint64_t, Mesh> meshCache;
    const Mesh& tessellationFor(const Solid& solid);
};

} // namespace cad
