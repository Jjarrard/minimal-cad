#pragma once
#include <cmath>
#include <algorithm>
#include <array>

namespace cad {

constexpr float PI = 3.14159265358979323846f;
constexpr float EPSILON = 1e-6f;
constexpr float DEG2RAD = PI / 180.0f;
constexpr float RAD2DEG = 180.0f / PI;

// ---- Vec2 ----
struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x, float y) : x(x), y(y) {}
    Vec2 operator+(Vec2 b) const { return {x + b.x, y + b.y}; }
    Vec2 operator-(Vec2 b) const { return {x - b.x, y - b.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(Vec2 b) { x += b.x; y += b.y; return *this; }
    Vec2& operator-=(Vec2 b) { x -= b.x; y -= b.y; return *this; }
    float dot(Vec2 b) const { return x * b.x + y * b.y; }
    float cross(Vec2 b) const { return x * b.y - y * b.x; }
    float length() const { return std::sqrt(x * x + y * y); }
    float lengthSq() const { return x * x + y * y; }
    Vec2 normalized() const { float l = length(); return l > EPSILON ? *this / l : Vec2{0,0}; }
    Vec2 perp() const { return {-y, x}; }
    float distTo(Vec2 b) const { return (*this - b).length(); }
};

// ---- Vec3 ----
struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    Vec3 operator+(Vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec3 operator-(Vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(Vec3 b) { x += b.x; y += b.y; z += b.z; return *this; }
    Vec3& operator-=(Vec3 b) { x -= b.x; y -= b.y; z -= b.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    bool operator==(Vec3 b) const { return std::abs(x-b.x)<EPSILON && std::abs(y-b.y)<EPSILON && std::abs(z-b.z)<EPSILON; }
    bool operator!=(Vec3 b) const { return !(*this == b); }
    float dot(Vec3 b) const { return x * b.x + y * b.y + z * b.z; }
    Vec3 cross(Vec3 b) const {
        return {y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x};
    }
    float length() const { return std::sqrt(x*x + y*y + z*z); }
    float lengthSq() const { return x*x + y*y + z*z; }
    Vec3 normalized() const { float l = length(); return l > EPSILON ? *this / l : Vec3{0,0,0}; }
    float distTo(Vec3 b) const { return (*this - b).length(); }
    const float* ptr() const { return &x; }
};

inline Vec3 operator*(float s, Vec3 v) { return v * s; }

// ---- Vec4 ----
struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    Vec4() = default;
    Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    Vec4(Vec3 v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}
};

// ---- Mat4 (column-major, OpenGL-compatible) ----
struct Mat4 {
    float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

    static Mat4 identity() { return {}; }

    static Mat4 translate(Vec3 t) {
        Mat4 r;
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }

    static Mat4 scale(Vec3 s) {
        Mat4 r;
        r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
        return r;
    }

    static Mat4 scale(float s) { return scale({s, s, s}); }

    static Mat4 rotateX(float rad) {
        Mat4 r;
        float c = std::cos(rad), s = std::sin(rad);
        r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
        return r;
    }

    static Mat4 rotateY(float rad) {
        Mat4 r;
        float c = std::cos(rad), s = std::sin(rad);
        r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
        return r;
    }

    static Mat4 rotateZ(float rad) {
        Mat4 r;
        float c = std::cos(rad), s = std::sin(rad);
        r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
        return r;
    }

    static Mat4 perspective(float fovRad, float aspect, float near, float far) {
        Mat4 r{};
        float t = std::tan(fovRad / 2.0f);
        r.m[0]  = 1.0f / (aspect * t);
        r.m[5]  = 1.0f / t;
        r.m[10] = -(far + near) / (far - near);
        r.m[11] = -1.0f;
        r.m[14] = -(2.0f * far * near) / (far - near);
        r.m[15] = 0.0f;
        return r;
    }

    static Mat4 ortho(float l, float r, float b, float t, float n, float f) {
        Mat4 m{};
        m.m[0]  = 2.0f / (r - l);
        m.m[5]  = 2.0f / (t - b);
        m.m[10] = -2.0f / (f - n);
        m.m[12] = -(r + l) / (r - l);
        m.m[13] = -(t + b) / (t - b);
        m.m[14] = -(f + n) / (f - n);
        return m;
    }

    static Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
        Vec3 f = (center - eye).normalized();
        Vec3 s = f.cross(up);
        // Looking straight along `up` leaves no unique right vector, and the
        // zero-safe normalize would hand back a zero basis — a matrix that
        // collapses the scene to nothing.  Fall back to another axis instead.
        if (s.lengthSq() < 1e-12f) {
            Vec3 alt = (std::abs(f.y) > 0.9f) ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
            s = f.cross(alt);
        }
        s = s.normalized();
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

    Mat4 operator*(const Mat4& b) const {
        Mat4 r{};
        for (int c = 0; c < 4; c++)
            for (int row = 0; row < 4; row++) {
                r.m[c*4+row] = 0;
                for (int k = 0; k < 4; k++)
                    r.m[c*4+row] += m[k*4+row] * b.m[c*4+k];
            }
        return r;
    }

    Vec4 operator*(Vec4 v) const {
        return {
            m[0]*v.x + m[4]*v.y + m[8]*v.z  + m[12]*v.w,
            m[1]*v.x + m[5]*v.y + m[9]*v.z  + m[13]*v.w,
            m[2]*v.x + m[6]*v.y + m[10]*v.z + m[14]*v.w,
            m[3]*v.x + m[7]*v.y + m[11]*v.z + m[15]*v.w
        };
    }

    Vec3 transformPoint(Vec3 p) const {
        Vec4 r = *this * Vec4(p, 1.0f);
        return {r.x, r.y, r.z};
    }

    Vec3 transformDir(Vec3 d) const {
        Vec4 r = *this * Vec4(d, 0.0f);
        return {r.x, r.y, r.z};
    }

    const float* ptr() const { return m; }
};

// ---- Ray ----
struct Ray {
    Vec3 origin, dir;
    Vec3 at(float t) const { return origin + dir * t; }
};

// ---- Plane ----
struct Plane {
    Vec3 normal;
    float d = 0; // normal.dot(pointOnPlane) = d

    Plane() = default;
    Plane(Vec3 n, float d) : normal(n.normalized()), d(d) {}
    Plane(Vec3 n, Vec3 point) : normal(n.normalized()), d(n.normalized().dot(point)) {}

    float distTo(Vec3 p) const { return normal.dot(p) - d; }
    Vec3 project(Vec3 p) const { return p - normal * distTo(p); }

    // Returns t such that ray.at(t) is on the plane. Returns -1 if parallel.
    float intersectRay(Ray ray) const {
        float denom = normal.dot(ray.dir);
        if (std::abs(denom) < EPSILON) return -1.0f;
        return (d - normal.dot(ray.origin)) / denom;
    }
};

// ---- AABB ----
struct AABB {
    Vec3 min{1e18f, 1e18f, 1e18f};
    Vec3 max{-1e18f, -1e18f, -1e18f};

    void expand(Vec3 p) {
        min.x = std::min(min.x, p.x); min.y = std::min(min.y, p.y); min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x); max.y = std::max(max.y, p.y); max.z = std::max(max.z, p.z);
    }
    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 size() const { return max - min; }
    float diagonal() const { return size().length(); }
};

// ---- Color ----
struct Color {
    float r = 1, g = 1, b = 1, a = 1;
    Color() = default;
    Color(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}
    static Color gray(float v) { return {v, v, v, 1}; }
    const float* ptr() const { return &r; }
};

// ---- Utility ----
inline float clamp01(float x) { return std::max(0.0f, std::min(1.0f, x)); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

// Closest point on a line segment ab to point p
inline Vec3 closestPointOnSegment(Vec3 a, Vec3 b, Vec3 p) {
    Vec3 ab = b - a;
    float t = ab.lengthSq();
    if (t < EPSILON) return a;
    t = clamp01((p - a).dot(ab) / t);
    return a + ab * t;
}

// Distance from point to line segment
inline float distPointToSegment(Vec3 p, Vec3 a, Vec3 b) {
    return p.distTo(closestPointOnSegment(a, b, p));
}

} // namespace cad
