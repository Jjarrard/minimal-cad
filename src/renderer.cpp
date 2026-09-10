#include "renderer.h"
#include <cstdio>
#include <vector>

namespace cad {

static int displayCircleSegments(float radius) {
    int segs = (int)std::ceil(radius * 2.0f);
    return std::max(160, std::min(720, segs));
}

// ---- Shaders ----

static const char* solidVertSrc = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
uniform mat4 uMVP;
uniform mat4 uModel;
out vec3 vNormal;
out vec3 vWorldPos;
void main() {
    vWorldPos = (uModel * vec4(aPos, 1.0)).xyz;
    vNormal = mat3(uModel) * aNormal;
    gl_Position = uMVP * vec4(aPos, 1.0);
})";

static const char* solidFragSrc = R"(#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;
uniform vec4 uColor;
uniform vec3 uLightDir;
uniform vec3 uViewPos;
out vec4 FragColor;
void main() {
    vec3 n = normalize(vNormal);
    // Two-sided lighting: bodies should look solid regardless of face winding.
    if (!gl_FrontFacing) n = -n;
    vec3 v = normalize(uViewPos - vWorldPos);
    vec3 l = normalize(uLightDir);

    // Hemisphere ambient — cool from the sky, warmer bounce from the ground.
    // A single flat ambient term is what makes untextured CAD look like putty;
    // varying it by surface orientation separates the faces on its own.
    float hemi = n.y * 0.5 + 0.5;
    vec3 ambient = mix(vec3(0.13, 0.14, 0.17), vec3(0.30, 0.32, 0.36), hemi);

    // Key light, plus a weak fill from the opposite side so the shadow side
    // keeps its shape instead of going dead flat.
    float key = max(dot(n, l), 0.0) * 0.62;
    vec3 fillDir = normalize(vec3(-l.x, 0.35, -l.z));
    float fill = max(dot(n, fillDir), 0.0) * 0.20;

    // Tight Blinn-Phong highlight — enough to read as a hard surface.
    vec3 h = normalize(l + v);
    float spec = pow(max(dot(n, h), 0.0), 56.0) * 0.28;

    // Fresnel rim to lift the silhouette off the background.
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0) * 0.13;

    vec3 color = uColor.rgb * (ambient + key + fill) + vec3(spec + rim);
    FragColor = vec4(color, uColor.a);
})";

static const char* lineVertSrc = R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
})";

static const char* lineFragSrc = R"(#version 330 core
uniform vec4 uColor;
out vec4 FragColor;
void main() {
    FragColor = uColor;
})";

static const char* gridVertSrc = R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uMVP;
out vec3 vWorldPos;
void main() {
    vWorldPos = aPos;
    gl_Position = uMVP * vec4(aPos, 1.0);
})";

static const char* gridFragSrc = R"(#version 330 core
in vec3 vWorldPos;
uniform vec4 uColor;
uniform float uFade;
out vec4 FragColor;
void main() {
    float dist = length(vWorldPos.xz);
    float alpha = uColor.a * (1.0 - smoothstep(uFade * 0.5, uFade, dist));
    FragColor = vec4(uColor.rgb, alpha);
})";

// Full-screen vertical gradient.  Positions arrive already in clip space, so
// there is no camera involved — this just paints the ground the model sits on.
static const char* bgVertSrc = R"(#version 330 core
layout(location=0) in vec3 aPos;
out float vT;
void main() {
    vT = aPos.y * 0.5 + 0.5;
    gl_Position = vec4(aPos.xy, 0.0, 1.0);
})";

static const char* bgFragSrc = R"(#version 330 core
in float vT;
uniform vec3 uTop;
uniform vec3 uBottom;
out vec4 FragColor;
void main() {
    FragColor = vec4(mix(uBottom, uTop, vT), 1.0);
})";

// ---- Shader methods ----

bool Shader::compile(const char* vertSrc, const char* fragSrc) {
    auto compileStage = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetShaderInfoLog(s, 512, nullptr, log);
            std::fprintf(stderr, "Shader error: %s\n", log);
            glDeleteShader(s);
            return 0;
        }
        return s;
    };

    GLuint vs = compileStage(GL_VERTEX_SHADER, vertSrc);
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragSrc);
    if (!vs || !fs) return false;

    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        std::fprintf(stderr, "Link error: %s\n", log);
        return false;
    }
    return true;
}

void Shader::use() const { glUseProgram(program); }

void Shader::setMat4(const char* name, const Mat4& m) const {
    glUniformMatrix4fv(glGetUniformLocation(program, name), 1, GL_FALSE, m.ptr());
}

void Shader::setVec3(const char* name, Vec3 v) const {
    glUniform3f(glGetUniformLocation(program, name), v.x, v.y, v.z);
}

void Shader::setVec4(const char* name, float x, float y, float z, float w) const {
    glUniform4f(glGetUniformLocation(program, name), x, y, z, w);
}

void Shader::setFloat(const char* name, float v) const {
    glUniform1f(glGetUniformLocation(program, name), v);
}

void Shader::destroy() {
    if (program) glDeleteProgram(program);
    program = 0;
}

// ---- GPUMesh methods ----

void GPUMesh::upload(const Mesh& mesh) {
    if (!vao) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
    }

    // Interleave position + normal
    std::vector<float> data;
    data.reserve(mesh.positions.size() * 6);
    for (size_t i = 0; i < mesh.positions.size(); i++) {
        data.push_back(mesh.positions[i].x);
        data.push_back(mesh.positions[i].y);
        data.push_back(mesh.positions[i].z);
        if (i < mesh.normals.size()) {
            data.push_back(mesh.normals[i].x);
            data.push_back(mesh.normals[i].y);
            data.push_back(mesh.normals[i].z);
        } else {
            data.push_back(0); data.push_back(1); data.push_back(0);
        }
    }

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(uint32_t), mesh.indices.data(), GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    indexCount = (uint32_t)mesh.indices.size();
}

void GPUMesh::uploadLines(const std::vector<Vec3>& points) {
    if (!vao) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
    }

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, points.size() * sizeof(Vec3), points.data(), GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vec3), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
    indexCount = (uint32_t)points.size();
}

void GPUMesh::draw(GLenum mode) const {
    if (!vao || indexCount == 0) return;
    glBindVertexArray(vao);
    if (mode == GL_TRIANGLES) {
        glDrawElements(mode, indexCount, GL_UNSIGNED_INT, 0);
    } else {
        glDrawArrays(mode, 0, indexCount);
    }
    glBindVertexArray(0);
}

void GPUMesh::destroy() {
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ebo) glDeleteBuffers(1, &ebo);
    vao = vbo = ebo = 0;
    indexCount = 0;
}

// ---- Renderer ----

bool Renderer::init() {
    if (!solidShader.compile(solidVertSrc, solidFragSrc)) return false;
    if (!lineShader.compile(lineVertSrc, lineFragSrc)) return false;
    if (!gridShader.compile(gridVertSrc, gridFragSrc)) return false;
    if (!bgShader.compile(bgVertSrc, bgFragSrc)) return false;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);

    return true;
}

void Renderer::shutdown() {
    solidShader.destroy();
    lineShader.destroy();
    gridShader.destroy();
    tempMesh.destroy();
    tempLines.destroy();
    gridMesh.destroy();
    gridMajorMesh.destroy();
}

void Renderer::beginFrame(int width, int height, float r, float g, float b) {
    glViewport(0, 0, width, height);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::endFrame() {
    // Nothing — GLFW handles swap
}

void Renderer::drawBackgroundGradient(Color top, Color bottom) {
    // A triangle strip covering clip space, drawn with depth writes off so it
    // never occludes the scene.
    static const std::vector<Vec3> quad = {
        {-1.0f, -1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f},
        {-1.0f,  1.0f, 0.0f}, { 1.0f,  1.0f, 0.0f},
    };
    tempLines.uploadLines(quad);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    bgShader.use();
    bgShader.setVec3("uTop", Vec3{top.r, top.g, top.b});
    bgShader.setVec3("uBottom", Vec3{bottom.r, bottom.g, bottom.b});
    tempLines.draw(GL_TRIANGLE_STRIP);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

// Content hash of a solid's geometry.  O(vertices) with a trivial constant —
// far cheaper than the ear-clipping it guards.
static uint64_t hashSolidGeometry(const Solid& solid) {
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](uint64_t v) { h ^= v; h *= 1099511628211ULL; };
    auto mixCoord = [&mix](float f) {
        mix((uint64_t)(int64_t)std::llround((double)f * 2048.0));
    };
    mix(solid.faces.size());
    for (const auto& face : solid.faces) {
        mix(face.outerLoop.size());
        for (const auto& p : face.outerLoop) {
            mixCoord(p.x); mixCoord(p.y); mixCoord(p.z);
        }
    }
    return h;
}

const Mesh& Renderer::tessellationFor(const Solid& solid) {
    uint64_t key = hashSolidGeometry(solid);
    auto it = meshCache.find(key);
    if (it != meshCache.end()) return it->second;

    // Bound the cache — every edit produces a new key, and stale entries are
    // only worth keeping for undo/redo round trips.
    if (meshCache.size() > 64) meshCache.clear();
    return meshCache.emplace(key, solid.tessellate()).first->second;
}

void Renderer::drawSolid(const Solid& solid, const Camera& cam, Color color, bool wireframe) {
    const Mesh& mesh = tessellationFor(solid);
    if (mesh.indices.empty()) return;

    tempMesh.upload(mesh);

    Mat4 model = Mat4::identity();
    Mat4 mvp = cam.projMatrix() * cam.viewMatrix() * model;

    solidShader.use();
    solidShader.setMat4("uMVP", mvp);
    solidShader.setMat4("uModel", model);
    solidShader.setVec4("uColor", color.r, color.g, color.b, color.a);
    solidShader.setVec3("uLightDir", Vec3{0.5f, 0.8f, 0.3f}.normalized());
    solidShader.setVec3("uViewPos", cam.getEyePos());

    if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    tempMesh.draw(GL_TRIANGLES);
    if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void Renderer::drawEdges(const Solid& solid, const Camera& cam, Color color, float lineWidth) {
    Wire w = solid.wireframe();
    if (w.points.empty()) return;

    tempLines.uploadLines(w.points);

    Mat4 mvp = cam.projMatrix() * cam.viewMatrix();
    lineShader.use();
    lineShader.setMat4("uMVP", mvp);
    lineShader.setVec4("uColor", color.r, color.g, color.b, color.a);

    glLineWidth(lineWidth);
    tempLines.draw(GL_LINES);
}

void Renderer::buildGrid(float step, float extent) {
    std::vector<Vec3> minor, major;
    int count = (int)(extent / step);
    for (int i = -count; i <= count; i++) {
        float v = i * step;
        // Every tenth line is a major one, giving the eye a coarse ruler to
        // read distance against instead of an undifferentiated mesh.
        std::vector<Vec3>& dst = (i % 10 == 0) ? major : minor;
        dst.push_back({v, 0, -extent});
        dst.push_back({v, 0, extent});
        dst.push_back({-extent, 0, v});
        dst.push_back({extent, 0, v});
    }
    gridMesh.uploadLines(minor);
    gridMajorMesh.uploadLines(major);
    gridBuilt = true;
    gridStepBuilt = step;
}

void Renderer::drawGrid(const Camera& cam, float, float) {
    // Pick a spacing from a 1 / 2 / 5 sequence so on-screen line density stays
    // roughly constant as you zoom.  A fixed 1 mm grid turns into unreadable
    // moiré the moment you pull back from a part.
    float target = std::max(cam.distance * 0.05f, 1e-4f);
    float mag = std::pow(10.0f, std::floor(std::log10(target)));
    float ratio = target / mag;
    float step = (ratio >= 5.0f) ? mag * 5.0f : (ratio >= 2.0f) ? mag * 2.0f : mag;

    float extent = step * 60.0f;
    if (!gridBuilt || std::abs(step - gridStepBuilt) > 1e-6f) buildGrid(step, extent);

    Mat4 mvp = cam.projMatrix() * cam.viewMatrix();
    gridShader.use();
    gridShader.setMat4("uMVP", mvp);
    gridShader.setFloat("uFade", extent);

    glLineWidth(1.0f);
    gridShader.setVec4("uColor", 0.42f, 0.45f, 0.52f, 0.16f);
    gridMesh.draw(GL_LINES);

    gridShader.setVec4("uColor", 0.50f, 0.54f, 0.62f, 0.34f);
    gridMajorMesh.draw(GL_LINES);

    // Ground axes.  Draw each as its own two-vertex batch — the previous code
    // drew X red and then re-drew both axes blue on top of it.
    lineShader.use();
    lineShader.setMat4("uMVP", mvp);
    glLineWidth(1.5f);

    std::vector<Vec3> xAxis = {{-extent, 0, 0}, {extent, 0, 0}};
    tempLines.uploadLines(xAxis);
    lineShader.setVec4("uColor", 0.80f, 0.32f, 0.32f, 0.55f);
    tempLines.draw(GL_LINES);

    std::vector<Vec3> zAxis = {{0, 0, -extent}, {0, 0, extent}};
    tempLines.uploadLines(zAxis);
    lineShader.setVec4("uColor", 0.36f, 0.52f, 0.90f, 0.55f);
    tempLines.draw(GL_LINES);
}

void Renderer::drawSketch(const Sketch& sketch, const Camera& cam) {
    Mat4 mvp = cam.projMatrix() * cam.viewMatrix();
    lineShader.use();
    lineShader.setMat4("uMVP", mvp);

    // Draw origin crosshair
    {
        float s = cam.distance * 0.04f;
        Vec3 o = sketch.sketchToWorld({0, 0});
        Vec3 uEnd = o + sketch.axisU * s;
        Vec3 vEnd = o + sketch.axisV * s;

        glLineWidth(2.5f);
        lineShader.setVec4("uColor", 0.9f, 0.25f, 0.25f, 0.9f);
        std::vector<Vec3> axPts = {o, uEnd};
        tempLines.uploadLines(axPts);
        tempLines.draw(GL_LINES);

        lineShader.setVec4("uColor", 0.25f, 0.8f, 0.25f, 0.9f);
        axPts = {o, vEnd};
        tempLines.uploadLines(axPts);
        tempLines.draw(GL_LINES);

        float d = cam.distance * 0.005f;
        Vec3 r = cam.getRightDir() * d;
        Vec3 u = cam.getUpDir() * d;
        lineShader.setVec4("uColor", 1.0f, 1.0f, 1.0f, 0.8f);
        glLineWidth(1.5f);
        axPts = {o - r, o + r, o - u, o + u};
        tempLines.uploadLines(axPts);
        tempLines.draw(GL_LINES);
    }

    // Draw sketch entities on top (disable depth test so they're always visible)
    glDisable(GL_DEPTH_TEST);
    glLineWidth(2.0f);

    for (const auto& e : sketch.entities) {
        Color c = e.selected ? Color{1.0f, 0.8f, 0.0f} :
                  e.construction ? Color{0.5f, 0.5f, 0.8f, 0.5f} :
                  Color{1.0f, 1.0f, 1.0f};
        lineShader.setVec4("uColor", c.r, c.g, c.b, c.a);

        std::vector<Vec3> pts;

        switch (e.type) {
        case SketchEntityType::Line:
            pts.push_back(sketch.sketchToWorld(e.p0));
            pts.push_back(sketch.sketchToWorld(e.p1));
            tempLines.uploadLines(pts);
            tempLines.draw(GL_LINES);
            break;

        case SketchEntityType::Circle: {
            int segs = displayCircleSegments(e.radius);
            for (int i = 0; i <= segs; i++) {
                float a = (float)i / segs * 2.0f * PI;
                Vec2 p = e.p0 + Vec2{std::cos(a) * e.radius, std::sin(a) * e.radius};
                pts.push_back(sketch.sketchToWorld(p));
            }
            tempLines.uploadLines(pts);
            tempLines.draw(GL_LINE_STRIP);
            break;
        }
        case SketchEntityType::Rectangle: {
            Vec3 c0 = sketch.sketchToWorld(e.p0);
            Vec3 c1 = sketch.sketchToWorld({e.p1.x, e.p0.y});
            Vec3 c2 = sketch.sketchToWorld(e.p1);
            Vec3 c3 = sketch.sketchToWorld({e.p0.x, e.p1.y});
            pts = {c0, c1, c1, c2, c2, c3, c3, c0};
            tempLines.uploadLines(pts);
            tempLines.draw(GL_LINES);
            break;
        }
        case SketchEntityType::Arc: {
            int segs = 32;
            Vec2 startOff = e.p1 - e.p0;
            Vec2 endOff = e.p2 - e.p0;
            float startAngle = std::atan2(startOff.y, startOff.x);
            float endAngle = std::atan2(endOff.y, endOff.x);
            if (endAngle < startAngle) endAngle += 2.0f * PI;
            for (int i = 0; i <= segs; i++) {
                float a = startAngle + (endAngle - startAngle) * (float)i / segs;
                Vec2 p = e.p0 + Vec2{std::cos(a) * e.radius, std::sin(a) * e.radius};
                pts.push_back(sketch.sketchToWorld(p));
            }
            tempLines.uploadLines(pts);
            tempLines.draw(GL_LINE_STRIP);
            break;
        }
        }
    }

    // Re-enable depth test after sketch entities
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawLine3D(Vec3 a, Vec3 b, const Camera& cam, Color color, float lineWidth) {
    std::vector<Vec3> pts = {a, b};
    tempLines.uploadLines(pts);

    Mat4 mvp = cam.projMatrix() * cam.viewMatrix();
    lineShader.use();
    lineShader.setMat4("uMVP", mvp);
    lineShader.setVec4("uColor", color.r, color.g, color.b, color.a);
    glLineWidth(lineWidth);
    tempLines.draw(GL_LINES);
}

void Renderer::drawSnapIndicator(Vec3 pos, const Camera& cam, SnapType type) {
    float size = cam.distance * 0.008f;
    Vec3 right = cam.getRightDir() * size;
    Vec3 up = cam.getUpDir() * size;

    Color c;
    std::vector<Vec3> pts;

    switch (type) {
    case SnapType::Endpoint:
        // Green filled square
        c = {0.0f, 1.0f, 0.2f};
        pts = {
            pos - right - up, pos + right - up,
            pos + right - up, pos + right + up,
            pos + right + up, pos - right + up,
            pos - right + up, pos - right - up,
        };
        break;
    case SnapType::Midpoint:
        // Cyan triangle
        c = {0.0f, 0.85f, 1.0f};
        pts = {
            pos - right - up * 0.7f, pos + right - up * 0.7f,
            pos + right - up * 0.7f, pos + up,
            pos + up, pos - right - up * 0.7f,
        };
        break;
    case SnapType::Center:
    case SnapType::Origin:
        // Orange circle (octagon)
        c = {1.0f, 0.6f, 0.0f};
        for (int i = 0; i < 8; i++) {
            float a1 = (float)i / 8.0f * 2.0f * PI;
            float a2 = (float)(i + 1) / 8.0f * 2.0f * PI;
            pts.push_back(pos + right * std::cos(a1) + up * std::sin(a1));
            pts.push_back(pos + right * std::cos(a2) + up * std::sin(a2));
        }
        // Plus a small cross inside
        {
            Vec3 rs = right * 0.5f, us = up * 0.5f;
            pts.push_back(pos - rs); pts.push_back(pos + rs);
            pts.push_back(pos - us); pts.push_back(pos + us);
        }
        break;
    case SnapType::OnLine:
        // X marker
        c = {1.0f, 0.8f, 0.2f};
        pts = {
            pos - right - up, pos + right + up,
            pos + right - up, pos - right + up,
        };
        break;
    case SnapType::Horizontal:
        // Horizontal double line
        c = {0.3f, 0.9f, 0.3f};
        pts = {
            pos - right, pos + right,
            pos - right + up * 0.3f, pos + right + up * 0.3f,
        };
        break;
    case SnapType::Vertical:
        // Vertical double line
        c = {0.3f, 0.9f, 0.3f};
        pts = {
            pos - up, pos + up,
            pos - up + right * 0.3f, pos + up + right * 0.3f,
        };
        break;
    case SnapType::Parallel:
        // Two parallel slash marks
        c = {0.9f, 0.7f, 0.2f};
        {
            Vec3 diag = (right + up) * 0.7f;
            Vec3 offset = right * 0.4f;
            pts = {
                pos - diag - offset, pos + diag - offset,
                pos - diag + offset, pos + diag + offset,
            };
        }
        break;
    case SnapType::Perpendicular:
        // Right-angle symbol
        c = {0.9f, 0.3f, 0.3f};
        pts = {
            pos - right, pos - right + up * 1.3f,
            pos - right, pos + right * 0.3f,
            // Small square in corner
            pos - right + up * 0.5f, pos - right * 0.5f + up * 0.5f,
            pos - right * 0.5f + up * 0.5f, pos - right * 0.5f,
        };
        break;
    default:
        c = {0.5f, 0.5f, 0.5f};
        pts = {
            pos + right, pos + up, pos - right, pos - up, pos + right,
        };
        break;
    }

    if (pts.empty()) return;
    tempLines.uploadLines(pts);

    Mat4 mvp = cam.projMatrix() * cam.viewMatrix();
    lineShader.use();
    lineShader.setMat4("uMVP", mvp);
    lineShader.setVec4("uColor", c.r, c.g, c.b, 1.0f);
    glLineWidth(2.5f);
    glDisable(GL_DEPTH_TEST);
    tempLines.draw(GL_LINES);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawConstraintSymbol(Vec3 pos, const Camera& cam, SnapType type, Vec3 refDir) {
    // Draw a small label-like symbol offset from the snap point
    float size = cam.distance * 0.012f;
    Vec3 right = cam.getRightDir() * size;
    Vec3 up = cam.getUpDir() * size;
    Vec3 offset = up * 2.5f + right * 1.5f; // offset up-right from cursor
    Vec3 center = pos + offset;

    std::vector<Vec3> pts;
    Color c;

    switch (type) {
    case SnapType::Horizontal:
        // H constraint: horizontal line with short verticals at ends
        c = {0.3f, 0.95f, 0.3f};
        pts = {
            center - right, center + right,
            center - right - up * 0.4f, center - right + up * 0.4f,
            center + right - up * 0.4f, center + right + up * 0.4f,
        };
        break;
    case SnapType::Vertical:
        c = {0.3f, 0.95f, 0.3f};
        pts = {
            center - up, center + up,
            center - up - right * 0.4f, center - up + right * 0.4f,
            center + up - right * 0.4f, center + up + right * 0.4f,
        };
        break;
    case SnapType::Parallel:
        // Two slashes
        c = {0.95f, 0.75f, 0.15f};
        {
            Vec3 diag = (right + up) * 0.6f;
            Vec3 off = right * 0.35f;
            pts = {
                center - diag - off, center + diag - off,
                center - diag + off, center + diag + off,
            };
        }
        break;
    case SnapType::Perpendicular:
        // Right-angle corner
        c = {0.95f, 0.35f, 0.35f};
        pts = {
            center - right * 0.6f, center - right * 0.6f + up,
            center - right * 0.6f, center + right * 0.4f,
        };
        break;
    default:
        return;
    }

    if (pts.empty()) return;
    tempLines.uploadLines(pts);

    Mat4 mvp = cam.projMatrix() * cam.viewMatrix();
    lineShader.use();
    lineShader.setMat4("uMVP", mvp);
    lineShader.setVec4("uColor", c.r, c.g, c.b, 1.0f);
    glLineWidth(2.0f);
    glDisable(GL_DEPTH_TEST);
    tempLines.draw(GL_LINES);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawFaceHighlight(const BRepFace& face, const Camera& cam, Color color) {
    if (face.outerLoop.size() < 3) return;

    Mesh mesh;
    const Vec3& v0 = face.outerLoop[0];
    for (size_t i = 1; i + 1 < face.outerLoop.size(); i++) {
        mesh.addTriangle(v0, face.outerLoop[i], face.outerLoop[i+1]);
    }

    tempMesh.upload(mesh);
    Mat4 mvp = cam.projMatrix() * cam.viewMatrix();

    solidShader.use();
    solidShader.setMat4("uMVP", mvp);
    solidShader.setMat4("uModel", Mat4::identity());
    solidShader.setVec4("uColor", color.r, color.g, color.b, color.a);
    solidShader.setVec3("uLightDir", Vec3{0.5f, 0.8f, 0.3f}.normalized());
    solidShader.setVec3("uViewPos", cam.getEyePos());

    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    tempMesh.draw(GL_TRIANGLES);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_POLYGON_OFFSET_FILL);
}

void Renderer::drawEdgeHighlight(Vec3 a, Vec3 b, const Camera& cam, Color color, float lineWidth) {
    drawLine3D(a, b, cam, color, lineWidth);
}

void Renderer::drawOriginCube(const Camera& cam, float size, int hoveredFace) {
    Mat4 mvp = cam.projMatrix() * cam.viewMatrix();
    float s = size;

    // Face 0: XY (Top) - lies on Y=0, normal (0,1,0), green
    // Face 1: XZ (Front) - lies on Z=0, normal (0,0,1), blue
    // Face 2: YZ (Side) - lies on X=0, normal (1,0,0), red
    struct CubeFace {
        Vec3 corners[4];
        Color baseColor;
        Color hoverColor;
    };

    CubeFace faces[3] = {
        // Top (XZ plane, Y=0): green
        {{{0,0,0}, {s,0,0}, {s,0,s}, {0,0,s}},
         {0.2f, 0.65f, 0.2f, 0.35f}, {0.3f, 0.85f, 0.3f, 0.55f}},
        // Front (XY plane, Z=0): blue
        {{{0,0,0}, {s,0,0}, {s,s,0}, {0,s,0}},
         {0.2f, 0.2f, 0.65f, 0.35f}, {0.3f, 0.3f, 0.85f, 0.55f}},
        // Side (YZ plane, X=0): red
        {{{0,0,0}, {0,0,s}, {0,s,s}, {0,s,0}},
         {0.65f, 0.2f, 0.2f, 0.35f}, {0.85f, 0.3f, 0.3f, 0.55f}},
    };

    solidShader.use();
    solidShader.setMat4("uMVP", mvp);
    solidShader.setMat4("uModel", Mat4::identity());
    solidShader.setVec3("uLightDir", Vec3{0.5f, 0.8f, 0.3f}.normalized());
    solidShader.setVec3("uViewPos", cam.getEyePos());

    glDepthFunc(GL_LEQUAL);

    for (int i = 0; i < 3; i++) {
        Color c = (i == hoveredFace) ? faces[i].hoverColor : faces[i].baseColor;
        solidShader.setVec4("uColor", c.r, c.g, c.b, c.a);

        Mesh mesh;
        mesh.addTriangle(faces[i].corners[0], faces[i].corners[1], faces[i].corners[2]);
        mesh.addTriangle(faces[i].corners[0], faces[i].corners[2], faces[i].corners[3]);
        tempMesh.upload(mesh);
        tempMesh.draw(GL_TRIANGLES);
    }

    glDepthFunc(GL_LESS);

    // Draw edges of each face
    lineShader.use();
    lineShader.setMat4("uMVP", mvp);
    glLineWidth(2.0f);

    Color edgeColors[3] = {
        {0.3f, 0.8f, 0.3f, 0.7f},
        {0.3f, 0.3f, 0.8f, 0.7f},
        {0.8f, 0.3f, 0.3f, 0.7f},
    };

    for (int i = 0; i < 3; i++) {
        float a = (i == hoveredFace) ? 1.0f : 0.7f;
        lineShader.setVec4("uColor", edgeColors[i].r, edgeColors[i].g, edgeColors[i].b, a);
        std::vector<Vec3> pts = {
            faces[i].corners[0], faces[i].corners[1],
            faces[i].corners[1], faces[i].corners[2],
            faces[i].corners[2], faces[i].corners[3],
            faces[i].corners[3], faces[i].corners[0],
        };
        tempLines.uploadLines(pts);
        tempLines.draw(GL_LINES);
    }

    // Draw axis lines from origin along each axis
    float axLen = s * 1.3f;
    lineShader.setVec4("uColor", 0.9f, 0.25f, 0.25f, 0.9f);
    glLineWidth(2.5f);
    std::vector<Vec3> ax = {{0,0,0}, {axLen,0,0}};
    tempLines.uploadLines(ax);
    tempLines.draw(GL_LINES);

    lineShader.setVec4("uColor", 0.25f, 0.8f, 0.25f, 0.9f);
    ax = {{0,0,0}, {0,axLen,0}};
    tempLines.uploadLines(ax);
    tempLines.draw(GL_LINES);

    lineShader.setVec4("uColor", 0.25f, 0.25f, 0.9f, 0.9f);
    ax = {{0,0,0}, {0,0,axLen}};
    tempLines.uploadLines(ax);
    tempLines.draw(GL_LINES);

    // Small origin dot
    lineShader.setVec4("uColor", 1.0f, 1.0f, 1.0f, 0.9f);
    float d = s * 0.08f;
    Vec3 r = cam.getRightDir() * d;
    Vec3 u = cam.getUpDir() * d;
    Vec3 o = {0,0,0};
    ax = {o-r, o+r, o-u, o+u};
    tempLines.uploadLines(ax);
    glLineWidth(2.0f);
    tempLines.draw(GL_LINES);
}

void Renderer::drawProfileFill(const std::vector<Vec3>& profile, const Camera& cam, Color color) {
    if (profile.size() < 3) return;

    // Compute normal
    Vec3 normal = (profile[1] - profile[0]).cross(profile[2] - profile[0]).normalized();

    Mesh mesh;
    triangulatePolygon(profile, normal, mesh.positions, mesh.normals, mesh.indices, 0);

    if (mesh.indices.empty()) return;
    tempMesh.upload(mesh);

    Mat4 mvp = cam.projMatrix() * cam.viewMatrix();
    solidShader.use();
    solidShader.setMat4("uMVP", mvp);
    solidShader.setMat4("uModel", Mat4::identity());
    solidShader.setVec4("uColor", color.r, color.g, color.b, color.a);
    solidShader.setVec3("uLightDir", Vec3{0.5f, 0.8f, 0.3f}.normalized());
    solidShader.setVec3("uViewPos", cam.getEyePos());

    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    tempMesh.draw(GL_TRIANGLES);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_POLYGON_OFFSET_FILL);
}

void Renderer::drawExtrudePreview(const Solid& solid, const Camera& cam, Color color) {
    Mesh mesh = solid.tessellate();
    if (mesh.indices.empty()) return;

    tempMesh.upload(mesh);
    Mat4 model = Mat4::identity();
    Mat4 mvp = cam.projMatrix() * cam.viewMatrix() * model;

    solidShader.use();
    solidShader.setMat4("uMVP", mvp);
    solidShader.setMat4("uModel", model);
    solidShader.setVec4("uColor", color.r, color.g, color.b, color.a);
    solidShader.setVec3("uLightDir", Vec3{0.5f, 0.8f, 0.3f}.normalized());
    solidShader.setVec3("uViewPos", cam.getEyePos());

    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    tempMesh.draw(GL_TRIANGLES);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_POLYGON_OFFSET_FILL);
}

} // namespace cad
