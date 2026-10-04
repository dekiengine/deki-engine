#pragma once

// Engine angle convention: radians, matching std::sin/cos/atan2.
// The inspector displays rotation fields as degrees via DEKI_UNIT(Angle) and
// the existing UnitDisplay mechanism; storage and runtime math stay in radians.

namespace Deki
{

namespace Math
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kHalfPi = 0.5f * kPi;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;

inline float Clamp(float v, float lo, float hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

inline float Min(float a, float b)
{
    return (a < b) ? a : b;
}
inline float Max(float a, float b)
{
    return (a > b) ? a : b;
}
inline float Abs(float v)
{
    return (v < 0.0f) ? -v : v;
}
inline float Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}
}  // namespace Math

}  // namespace Deki
