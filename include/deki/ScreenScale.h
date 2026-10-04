#pragma once

#include <cmath>

// How a camera's framing maps onto a screen.
//
// An orthographic camera shows a fixed height of world, its ortho height in
// meters; the width follows the screen's shape. Every screen renders at its
// own native size, so this decides how many of its pixels make one meter:
//
//   screenHeight / orthoHeight
//
// A bigger screen of the same shape shows the same world, drawn bigger; a
// wider one shows more at the sides.
//
// With pixel perfect, the result is snapped down to a whole multiple of the
// art density (pixelsPerMeter), never below 1x, so every art pixel covers the
// same whole number of screen pixels. The camera then shows slightly more than
// its ortho height, or, on a screen too small for 1x, crops.
//
// The camera, the editor and 3D passes all call this, so they agree exactly.

namespace Deki
{

/// Whole art-pixel multiple for pixel-perfect scaling, at least 1. The small
/// bias keeps an exact fit that float division puts just under (3.9999) at 4.
inline int PixelPerfectMultiple(float scale)
{
    const int k = static_cast<int>(std::floor(scale + 1e-4f));
    return k < 1 ? 1 : k;
}

/// Screen pixels per meter for a screen this tall showing orthoHeight meters.
/// Returns 0 for an empty screen or a non-positive height; callers draw
/// nothing then.
inline float OrthoPixelsPerMeter(int screenHeight, float orthoHeight, bool pixelPerfect, float artPixelsPerMeter)
{
    if (screenHeight <= 0 || orthoHeight <= 0.0f)
    {
        return 0.0f;
    }
    const float fit = static_cast<float>(screenHeight) / orthoHeight;
    if (!pixelPerfect || artPixelsPerMeter <= 0.0f)
    {
        return fit;
    }
    return artPixelsPerMeter * static_cast<float>(PixelPerfectMultiple(fit / artPixelsPerMeter));
}

}  // namespace Deki
