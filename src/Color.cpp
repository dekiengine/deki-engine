#include <deki/Color.h>
#include <cmath>
#include <cstring>

namespace Deki
{

// Predefined colors are inline static const in the header.

Color Color::FromFloats(float r, float g, float b, float a)
{
    return Color(static_cast<uint8_t>(std::min(std::max(r * 255.0f, 0.0f), 255.0f)),
                 static_cast<uint8_t>(std::min(std::max(g * 255.0f, 0.0f), 255.0f)),
                 static_cast<uint8_t>(std::min(std::max(b * 255.0f, 0.0f), 255.0f)),
                 static_cast<uint8_t>(std::min(std::max(a * 255.0f, 0.0f), 255.0f)));
}

Color Color::FromHSV(float h, float s, float v, float a)
{
    // Normalize hue to 0-360 range
    while (h < 0)
    {
        h += 360;
    }
    while (h >= 360)
    {
        h -= 360;
    }

    float c = v * s;
    float x = c * (1 - std::abs(std::fmod(h / 60.0f, 2) - 1));
    float m = v - c;

    float r, g, b;

    if (h < 60)
    {
        r = c;
        g = x;
        b = 0;
    }
    else if (h < 120)
    {
        r = x;
        g = c;
        b = 0;
    }
    else if (h < 180)
    {
        r = 0;
        g = c;
        b = x;
    }
    else if (h < 240)
    {
        r = 0;
        g = x;
        b = c;
    }
    else if (h < 300)
    {
        r = x;
        g = 0;
        b = c;
    }
    else
    {
        r = c;
        g = 0;
        b = x;
    }

    return FromFloats(r + m, g + m, b + m, a);
}

void Color::ToFloats(float& fr, float& fg, float& fb, float& fa) const
{
    fr = r / 255.0f;
    fg = g / 255.0f;
    fb = b / 255.0f;
    fa = a / 255.0f;
}

void Color::ToHSV(float& h, float& s, float& v) const
{
    float fr, fg, fb, fa;
    ToFloats(fr, fg, fb, fa);

    float maxVal = std::max({ fr, fg, fb });
    float minVal = std::min({ fr, fg, fb });
    float delta = maxVal - minVal;

    // Value
    v = maxVal;

    // Saturation
    if (maxVal == 0)
    {
        s = 0;
        h = 0;
        return;
    }
    s = delta / maxVal;

    // Hue
    if (delta == 0)
    {
        h = 0;
    }
    else if (maxVal == fr)
    {
        h = 60 * (std::fmod((fg - fb) / delta, 6));
    }
    else if (maxVal == fg)
    {
        h = 60 * ((fb - fr) / delta + 2);
    }
    else
    {
        h = 60 * ((fr - fg) / delta + 4);
    }

    if (h < 0)
    {
        h += 360;
    }
}

Color Color::Lerp(const Color& to, float t) const
{
    t = std::min(std::max(t, 0.0f), 1.0f);
    return Color(static_cast<uint8_t>(r + t * (to.r - r)), static_cast<uint8_t>(g + t * (to.g - g)),
                 static_cast<uint8_t>(b + t * (to.b - b)), static_cast<uint8_t>(a + t * (to.a - a)));
}

Color Color::Multiply(float factor) const
{
    return Color(static_cast<uint8_t>(std::min(r * factor, 255.0f)), static_cast<uint8_t>(std::min(g * factor, 255.0f)),
                 static_cast<uint8_t>(std::min(b * factor, 255.0f)), a);
}

Color Color::Tint(const Color& tintColor, float intensity) const
{
    intensity = std::min(std::max(intensity, 0.0f), 1.0f);
    return Lerp(tintColor, intensity);
}

Color Color::ToGrayscale() const
{
    uint8_t gray = GetLuminance();
    return Color(gray, gray, gray, a);
}

Color Color::AdjustBrightness(float factor) const
{
    return Multiply(factor);
}

Color Color::AdjustSaturation(float factor) const
{
    float h, s, v;
    ToHSV(h, s, v);
    s = std::min(std::max(s * factor, 0.0f), 1.0f);
    return FromHSV(h, s, v, a / 255.0f);
}

}  // namespace Deki