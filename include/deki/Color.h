#pragma once

#include <cstdint>
#include <algorithm>
#include <cstring>

namespace Deki
{

/**
 * @class Color
 * @brief RGBA colour with packed-format conversions (RGB565, RGBA8888).
 *
 * The Color class provides a unified interface for color handling throughout the engine,
 * supporting RGB, RGBA, and various packed formats with conversion utilities.
 */
class Color
{
public:
    // Color components (0-255 range)
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;

    // Constructors
    Color()
        : r(0), g(0), b(0), a(255) {}
    Color(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
        : r(red), g(green), b(blue), a(alpha) {}

    // Create from packed formats
    static Color FromRGB565(uint16_t rgb565);
    static Color FromRGB888(uint32_t rgb888);
    static Color FromARGB8888(uint32_t argb8888);
    static Color FromRGBA8888(uint32_t rgba8888);

    // Create from normalized floats (0.0-1.0)
    static Color FromFloats(float r, float g, float b, float a = 1.0f);

    // Create from HSV
    static Color FromHSV(float h, float s, float v, float a = 1.0f);

    // Convert to packed formats (optimized for fast execution)
    uint16_t ToRGB565() const;
    uint32_t ToRGB888() const;
    uint32_t ToARGB8888() const;
    uint32_t ToRGBA8888() const;
    uint32_t ToABGR8888() const;  // ImGui-compatible format

    // Convert to normalized floats
    void ToFloats(float& r, float& g, float& b, float& a) const;

    // Convert to HSV
    void ToHSV(float& h, float& s, float& v) const;

    // Color operations
    Color Lerp(const Color& to, float t) const;
    Color Multiply(float factor) const;
    Color Tint(const Color& tintColor, float intensity) const;
    Color WithAlpha(uint8_t newAlpha) const;

    // Utility functions
    uint8_t GetLuminance() const;
    Color ToGrayscale() const;
    Color AdjustBrightness(float factor) const;
    Color AdjustSaturation(float factor) const;

    // Comparison operators
    bool operator==(const Color& other) const
    {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }

    bool operator!=(const Color& other) const
    {
        return !(*this == other);
    }

    // Predefined colors (declared here, defined below after class is complete)
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
};

// =============================================================================
// Inline definitions of predefined colors (for DLL export compatibility)
// =============================================================================
// These are inline to avoid linker errors when multiple DLLs use them.
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

// Inline implementations for performance (platform-optimized)
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

    // Fill in the lower bits for better color accuracy
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

inline uint8_t Color::GetLuminance() const
{
    // Using standard luminance calculation
    return static_cast<uint8_t>(0.299f * r + 0.587f * g + 0.114f * b);
}

}  // namespace Deki

// =============================================================================
// DEKI_COL32 macro - pack RGBA to ABGR format (ImGui-compatible)
// =============================================================================
// This macro creates a 32-bit color in ABGR byte order, matching ImGui's IM_COL32.
// Use this for passing colors to editor rendering functions.
//
// Usage: uint32_t color = DEKI_COL32(255, 128, 64, 200);
//
#ifndef DEKI_COL32
#define DEKI_COL32(R, G, B, A) (((uint32_t)(A) << 24) | ((uint32_t)(B) << 16) | ((uint32_t)(G) << 8) | ((uint32_t)(R)))
#endif