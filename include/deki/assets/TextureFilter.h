#pragma once

// How a texture is sampled where a GPU draws it: an image's setting, per
// platform, that 2D sprites and 3D meshes both follow.
//
//   Point      each pixel takes the nearest texel: sharp, for pixel art.
//   Bilinear   blends the four nearest texels: smooth when an image is drawn
//              near its own size or larger.
//   Trilinear  bilinear, from smaller copies of the image (mipmaps) blended
//              by how small it is drawn: steady at any size, for 3D at a
//              distance and sprites drawn shrunk.
//
// A platform that draws without a GPU (a board's software renderer) draws
// Point whatever the image asks; the editor shows the image as Point there.

#include <cstdint>
#include <cstring>

namespace Deki
{

enum class TextureFilter : uint8_t
{
    Point = 0,
    Bilinear = 1,
    Trilinear = 2,
};

/// Where a filter sits in a compiled image's header flags (Texture2D's
/// DTEX_FLAG_*): bits 4 and 5. 0 is Point, so a file from before reads as it
/// drew then.
constexpr uint32_t kTextureFilterFlagShift = 4;
constexpr uint32_t kTextureFilterFlagMask = 0x3u << kTextureFilterFlagShift;

inline TextureFilter TextureFilterFromFlags(uint32_t flags)
{
    const uint32_t v = (flags & kTextureFilterFlagMask) >> kTextureFilterFlagShift;
    return v <= static_cast<uint32_t>(TextureFilter::Trilinear) ? static_cast<TextureFilter>(v) : TextureFilter::Point;
}

inline uint32_t TextureFilterToFlags(TextureFilter filter)
{
    return (static_cast<uint32_t>(filter) << kTextureFilterFlagShift) & kTextureFilterFlagMask;
}

inline const char* TextureFilterName(TextureFilter filter)
{
    switch (filter)
    {
        case TextureFilter::Point: return "Point";
        case TextureFilter::Bilinear: return "Bilinear";
        case TextureFilter::Trilinear: return "Trilinear";
    }
    return "Point";
}

/// "Point", "Bilinear" or "Trilinear". False, leaving `out` alone, for
/// anything else.
inline bool ParseTextureFilter(const char* name, TextureFilter& out)
{
    for (uint32_t i = 0; i <= static_cast<uint32_t>(TextureFilter::Trilinear); ++i)
    {
        const TextureFilter f = static_cast<TextureFilter>(i);
        if (name && std::strcmp(name, TextureFilterName(f)) == 0)
        {
            out = f;
            return true;
        }
    }
    return false;
}

}  // namespace Deki
