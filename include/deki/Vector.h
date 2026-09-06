#pragma once

#include <deki/Math.h>
#include <cmath>

/**
 * @brief 2D vector for positions, directions, and scales.
 */

namespace Deki
{

struct Vector2
{
    float x;
    float y;

    // Constructors
    Vector2()
        : x(0.0f), y(0.0f) {}
    Vector2(float x_, float y_)
        : x(x_), y(y_) {}

    // Static constants
    static Vector2 Zero() { return Vector2(0.0f, 0.0f); }
    static Vector2 One() { return Vector2(1.0f, 1.0f); }
    static Vector2 Up() { return Vector2(0.0f, 1.0f); }
    static Vector2 Down() { return Vector2(0.0f, -1.0f); }
    static Vector2 Left() { return Vector2(-1.0f, 0.0f); }
    static Vector2 Right() { return Vector2(1.0f, 0.0f); }

    // Operators
    Vector2 operator+(const Vector2& o) const { return Vector2(x + o.x, y + o.y); }
    Vector2 operator-(const Vector2& o) const { return Vector2(x - o.x, y - o.y); }
    Vector2 operator*(float s) const { return Vector2(x * s, y * s); }
    Vector2 operator/(float s) const { return Vector2(x / s, y / s); }
    Vector2 operator*(const Vector2& o) const { return Vector2(x * o.x, y * o.y); }
    Vector2 operator/(const Vector2& o) const { return Vector2(x / o.x, y / o.y); }

    Vector2& operator+=(const Vector2& o)
    {
        x += o.x;
        y += o.y;
        return *this;
    }
    Vector2& operator-=(const Vector2& o)
    {
        x -= o.x;
        y -= o.y;
        return *this;
    }
    Vector2& operator*=(float s)
    {
        x *= s;
        y *= s;
        return *this;
    }
    Vector2& operator/=(float s)
    {
        x /= s;
        y /= s;
        return *this;
    }

    bool operator==(const Vector2& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Vector2& o) const { return !(*this == o); }

    Vector2 operator-() const { return Vector2(-x, -y); }

    float LengthSquared() const { return x * x + y * y; }
    float Length() const { return std::sqrt(LengthSquared()); }

    Vector2 Normalized() const
    {
        float len = Length();
        if (len > 0.0f) return *this / len;
        return Zero();
    }

    void Normalize()
    {
        float len = Length();
        if (len > 0.0f)
        {
            x /= len;
            y /= len;
        }
    }

    float Dot(const Vector2& o) const { return x * o.x + y * o.y; }
    float Cross(const Vector2& o) const { return x * o.y - y * o.x; }

    float DistanceTo(const Vector2& o) const { return (*this - o).Length(); }
    float DistanceSquaredTo(const Vector2& o) const { return (*this - o).LengthSquared(); }

    static Vector2 Lerp(const Vector2& a, const Vector2& b, float t)
    {
        return Vector2(Math::Lerp(a.x, b.x, t), Math::Lerp(a.y, b.y, t));
    }

    /// Rotate by angle in radians.
    Vector2 Rotated(float radians) const
    {
        float c = std::cos(radians);
        float s = std::sin(radians);
        return Vector2(x * c - y * s, x * s + y * c);
    }

    /// Angle of this vector relative to +X axis, in radians.
    float Angle() const { return std::atan2(y, x); }
    float AngleTo(const Vector2& o) const { return std::atan2(Cross(o), Dot(o)); }
};

inline Vector2 operator*(float s, const Vector2& v)
{
    return v * s;
}

/**
 * @brief 3D vector for positions, directions, scales, and rotations.
 */
struct Vector3
{
    float x;
    float y;
    float z;

    Vector3()
        : x(0.0f), y(0.0f), z(0.0f) {}
    Vector3(float x_, float y_, float z_)
        : x(x_), y(y_), z(z_) {}
    Vector3(const Vector2& v, float z_ = 0.0f)
        : x(v.x), y(v.y), z(z_) {}

    static Vector3 Zero() { return Vector3(0.0f, 0.0f, 0.0f); }
    static Vector3 One() { return Vector3(1.0f, 1.0f, 1.0f); }
    static Vector3 Up() { return Vector3(0.0f, 1.0f, 0.0f); }
    static Vector3 Down() { return Vector3(0.0f, -1.0f, 0.0f); }
    static Vector3 Left() { return Vector3(-1.0f, 0.0f, 0.0f); }
    static Vector3 Right() { return Vector3(1.0f, 0.0f, 0.0f); }
    static Vector3 Forward() { return Vector3(0.0f, 0.0f, 1.0f); }
    static Vector3 Back() { return Vector3(0.0f, 0.0f, -1.0f); }

    Vector2 ToVector2() const { return Vector2(x, y); }

    Vector3 operator+(const Vector3& o) const { return Vector3(x + o.x, y + o.y, z + o.z); }
    Vector3 operator-(const Vector3& o) const { return Vector3(x - o.x, y - o.y, z - o.z); }
    Vector3 operator*(float s) const { return Vector3(x * s, y * s, z * s); }
    Vector3 operator/(float s) const { return Vector3(x / s, y / s, z / s); }
    Vector3 operator*(const Vector3& o) const { return Vector3(x * o.x, y * o.y, z * o.z); }
    Vector3 operator/(const Vector3& o) const { return Vector3(x / o.x, y / o.y, z / o.z); }

    Vector3& operator+=(const Vector3& o)
    {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }
    Vector3& operator-=(const Vector3& o)
    {
        x -= o.x;
        y -= o.y;
        z -= o.z;
        return *this;
    }
    Vector3& operator*=(float s)
    {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }
    Vector3& operator/=(float s)
    {
        x /= s;
        y /= s;
        z /= s;
        return *this;
    }

    bool operator==(const Vector3& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const Vector3& o) const { return !(*this == o); }

    Vector3 operator-() const { return Vector3(-x, -y, -z); }

    float LengthSquared() const { return x * x + y * y + z * z; }
    float Length() const { return std::sqrt(LengthSquared()); }

    Vector3 Normalized() const
    {
        float len = Length();
        if (len > 0.0f) return *this / len;
        return Zero();
    }

    void Normalize()
    {
        float len = Length();
        if (len > 0.0f)
        {
            x /= len;
            y /= len;
            z /= len;
        }
    }

    float Dot(const Vector3& o) const
    {
        return x * o.x + y * o.y + z * o.z;
    }

    Vector3 Cross(const Vector3& o) const
    {
        return Vector3(
            y * o.z - z * o.y,
            z * o.x - x * o.z,
            x * o.y - y * o.x);
    }

    float DistanceTo(const Vector3& o) const { return (*this - o).Length(); }
    float DistanceSquaredTo(const Vector3& o) const { return (*this - o).LengthSquared(); }

    static Vector3 Lerp(const Vector3& a, const Vector3& b, float t)
    {
        return Vector3(Math::Lerp(a.x, b.x, t), Math::Lerp(a.y, b.y, t), Math::Lerp(a.z, b.z, t));
    }
};

inline Vector3 operator*(float s, const Vector3& v)
{
    return v * s;
}

/**
 * @brief 4x4 column-major matrix used as the OpenGL/GLSL FFI bridge.
 *
 * Storage: column-major (m[col][row]). Identity by default.
 */
struct Mat4
{
    float m[4][4];

    Mat4()
    {
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                m[c][r] = (c == r) ? 1.0f : 0.0f;
    }

    static Mat4 Identity() { return Mat4{}; }

    static Mat4 Ortho(float left, float right, float bottom, float top,
                      float nearZ, float farZ)
    {
        Mat4 r;
        const float rl = (right - left);
        const float tb = (top - bottom);
        const float fn = (farZ - nearZ);
        if (rl == 0.0f || tb == 0.0f || fn == 0.0f) return r;  // identity on degenerate
        r.m[0][0] = 2.0f / rl;
        r.m[1][1] = 2.0f / tb;
        r.m[2][2] = -2.0f / fn;
        r.m[3][0] = -(right + left) / rl;
        r.m[3][1] = -(top + bottom) / tb;
        r.m[3][2] = -(farZ + nearZ) / fn;
        r.m[3][3] = 1.0f;
        return r;
    }
};

}  // namespace Deki
