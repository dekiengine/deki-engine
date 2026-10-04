#pragma once

#include <cstdint>
#include <algorithm>
#include <cstring>

namespace Deki
{

/// An RGBA colour, 8 bits per channel, with conversions to and from packed
/// formats (RGB565, RGB888, ARGB8888, RGBA8888), floats and HSV.
class Color
{
public:
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;

    Color()
        : r(0),
          g(0),
          b(0),
          a(255)
    {
    }
    Color(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
        : r(red),
          g(green),
          b(blue),
          a(alpha)
    {
    }

    static Color FromRGB565(uint16_t rgb565);
    static Color FromRGB888(uint32_t rgb888);
    static Color FromARGB8888(uint32_t argb8888);
    static Color FromRGBA8888(uint32_t rgba8888);

    /// Channels in 0..1.
    static Color FromFloats(float r, float g, float b, float a = 1.0f);

    static Color FromHSV(float h, float s, float v, float a = 1.0f);

    uint16_t ToRGB565() const;
    uint32_t ToRGB888() const;
    uint32_t ToARGB8888() const;
    uint32_t ToRGBA8888() const;
    uint32_t ToABGR8888() const;  // ImGui-compatible format

    /// Channels in 0..1.
    void ToFloats(float& r, float& g, float& b, float& a) const;

    void ToHSV(float& h, float& s, float& v) const;

    Color Lerp(const Color& to, float t) const;
    Color Multiply(float factor) const;
    Color Tint(const Color& tintColor, float intensity) const;
    Color WithAlpha(uint8_t newAlpha) const;

    uint8_t GetLuminance() const;
    Color ToGrayscale() const;
    Color AdjustBrightness(float factor) const;
    Color AdjustSaturation(float factor) const;

    bool operator==(const Color& other) const { return r == other.r && g == other.g && b == other.b && a == other.a; }

    bool operator!=(const Color& other) const { return !(*this == other); }

    // Named colours, defined below once the class is complete.
    // NOLINTBEGIN(readability-identifier-naming): named values, used like enum values
    static const Color Black;
    static const Color White;
    static const Color Red;
    static const Color Green;
    static const Color Blue;
    static const Color Yellow;
    static const Color Cyan;
    static const Color Magenta;
    static const Color Gray;
    static const Color DarkGray;
    static const Color LightGray;
    static const Color Orange;
    static const Color Purple;
    static const Color Brown;
    static const Color Pink;
    static const Color Transparent;

    // Common game colors
    static const Color SkyBlue;
    static const Color GrassGreen;
    static const Color SunsetOrange;
    static const Color NightBlue;
    // NOLINTEND(readability-identifier-naming)
};

// Inline so several DLLs can use them without duplicate-symbol link errors.
// NOLINTBEGIN(readability-identifier-naming)
inline const Color Color::Black{ 0, 0, 0 };
inline const Color Color::White{ 255, 255, 255 };
inline const Color Color::Red{ 255, 0, 0 };
inline const Color Color::Green{ 0, 255, 0 };
inline const Color Color::Blue{ 0, 0, 255 };
inline const Color Color::Yellow{ 255, 255, 0 };
inline const Color Color::Cyan{ 0, 255, 255 };
inline const Color Color::Magenta{ 255, 0, 255 };
inline const Color Color::Gray{ 128, 128, 128 };
inline const Color Color::DarkGray{ 64, 64, 64 };
inline const Color Color::LightGray{ 192, 192, 192 };
inline const Color Color::Orange{ 255, 165, 0 };
inline const Color Color::Purple{ 128, 0, 128 };
inline const Color Color::Brown{ 139, 69, 19 };
inline const Color Color::Pink{ 255, 192, 203 };
inline const Color Color::Transparent{ 0, 0, 0, 0 };
inline const Color Color::SkyBlue{ 135, 206, 235 };
inline const Color Color::GrassGreen{ 124, 252, 0 };
inline const Color Color::SunsetOrange{ 255, 94, 77 };
inline const Color Color::NightBlue{ 25, 25, 112 };
// NOLINTEND(readability-identifier-naming)

DEKI_FAST_ATTR inline uint16_t Color::ToRGB565() const
{
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

DEKI_FAST_ATTR inline uint32_t Color::ToRGB888() const
{
    return (r << 16) | (g << 8) | b;
}

DEKI_FAST_ATTR inline uint32_t Color::ToARGB8888() const
{
    return (a << 24) | (r << 16) | (g << 8) | b;
}

DEKI_FAST_ATTR inline uint32_t Color::ToRGBA8888() const
{
    return (r << 24) | (g << 16) | (b << 8) | a;
}

DEKI_FAST_ATTR inline uint32_t Color::ToABGR8888() const
{
    return (a << 24) | (b << 16) | (g << 8) | r;
}

DEKI_FAST_ATTR inline Color Color::FromRGB565(uint16_t rgb565)
{
    uint8_t r = ((rgb565 >> 11) & 0x1F) << 3;
    uint8_t g = ((rgb565 >> 5) & 0x3F) << 2;
    uint8_t b = (rgb565 & 0x1F) << 3;

    // Copy the top bits into the empty low bits, so 0x1F maps to 255, not 248.
    r |= (r >> 5);
    g |= (g >> 6);
    b |= (b >> 5);

    return Color(r, g, b);
}

DEKI_FAST_ATTR inline Color Color::FromRGB888(uint32_t rgb888)
{
    uint8_t r = (rgb888 >> 16) & 0xFF;
    uint8_t g = (rgb888 >> 8) & 0xFF;
    uint8_t b = rgb888 & 0xFF;
    return Color(r, g, b);
}

DEKI_FAST_ATTR inline Color Color::FromARGB8888(uint32_t argb8888)
{
    uint8_t a = (argb8888 >> 24) & 0xFF;
    uint8_t r = (argb8888 >> 16) & 0xFF;
    uint8_t g = (argb8888 >> 8) & 0xFF;
    uint8_t b = argb8888 & 0xFF;
    return Color(r, g, b, a);
}

inline Color Color::FromRGBA8888(uint32_t rgba8888)
{
    uint8_t r = (rgba8888 >> 24) & 0xFF;
    uint8_t g = (rgba8888 >> 16) & 0xFF;
    uint8_t b = (rgba8888 >> 8) & 0xFF;
    uint8_t a = rgba8888 & 0xFF;
    return Color(r, g, b, a);
}

inline Color Color::WithAlpha(uint8_t newAlpha) const
{
    return Color(r, g, b, newAlpha);
}

/// Writes `c` as "#RRGGBBAA", the form saved files use. Saved files must not
/// store a packed number: it is unreadable in a diff and ties every file to
/// how Color packs its channels.
inline void ColorToHex(const Color& c, char out[10])
{
    static const char* const kDigits = "0123456789ABCDEF";
    const uint8_t v[4] = { c.r, c.g, c.b, c.a };
    out[0] = '#';
    for (int i = 0; i < 4; ++i)
    {
        out[1 + i * 2] = kDigits[(v[i] >> 4) & 0xF];
        out[2 + i * 2] = kDigits[v[i] & 0xF];
    }
    out[9] = '\0';
}

/// Parses "#RRGGBBAA", and "#RRGGBB" as fully opaque. False on anything else,
/// leaving `out` untouched, so a malformed value keeps the field's default
/// rather than turning into black.
inline bool ColorFromHex(const char* text, Color& out)
{
    if (!text || *text != '#')
    {
        return false;
    }
    const char* p = text + 1;
    int digits = 0;
    while (p[digits] && digits < 9)
    {
        ++digits;
    }
    if (digits != 6 && digits != 8)
    {
        return false;
    }

    uint8_t v[4] = { 0, 0, 0, 255 };
    for (int i = 0; i < digits / 2; ++i)
    {
        int byte = 0;
        for (int half = 0; half < 2; ++half)
        {
            const char ch = p[i * 2 + half];
            int d;
            if (ch >= '0' && ch <= '9')
            {
                d = ch - '0';
            }
            else if (ch >= 'a' && ch <= 'f')
            {
                d = ch - 'a' + 10;
            }
            else if (ch >= 'A' && ch <= 'F')
            {
                d = ch - 'A' + 10;
            }
            else
            {
                return false;
            }
            byte = (byte << 4) | d;
        }
        v[i] = static_cast<uint8_t>(byte);
    }
    out = Color(v[0], v[1], v[2], v[3]);
    return true;
}

inline uint8_t Color::GetLuminance() const
{
    // ITU-R BT.601 luma weights.
    return static_cast<uint8_t>(0.299f * r + 0.587f * g + 0.114f * b);
}

}  // namespace Deki

// Packs RGBA into a 32-bit ABGR value, the same as ImGui's IM_COL32. Use it
// for colours passed to editor drawing functions:
//   uint32_t color = DEKI_COL32(255, 128, 64, 200);
#ifndef DEKI_COL32
#define DEKI_COL32(R, G, B, A) (((uint32_t)(A) << 24) | ((uint32_t)(B) << 16) | ((uint32_t)(G) << 8) | ((uint32_t)(R)))
#endif