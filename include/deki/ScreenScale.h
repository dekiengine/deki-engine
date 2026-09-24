#pragma once

#include <cmath>
#include <cstdint>
#include <deki/Engine.h>

/**
 * @brief How the project's design area maps onto a screen.
 *
 * The design area (EngineSettings::designWidth x designHeight, meters) is what
 * the player sees. Every screen renders at its own native size; this decides
 * how many of its pixels make one meter:
 *
 *   ShowAll  min(screenW / designW, screenH / designH)  whole area visible
 *   Fill     max(...)                                    whole screen covered
 *
 * With pixelPerfect, the result is snapped down to a whole multiple of the art
 * density (pixelsPerMeter), never below 1x, so every art pixel covers the same
 * whole number of screen pixels. A screen smaller than the design area then
 * stays at 1x and crops.
 *
 * The camera, the editor and 3D passes all call this, so they agree exactly.
 */

namespace Deki
{

/// Whole art-pixel multiple for pixel-perfect scaling, at least 1. The small
/// bias keeps an exact fit that float division lands just under (3.9999) on 4.
inline int PixelPerfectMultiple(float scale)
{
    const int k = static_cast<int>(std::floor(scale + 1e-4f));
    return k < 1 ? 1 : k;
}

/// Screen pixels per meter at zoom 1 for a screen of this size. Returns 0 for
/// an empty screen or an empty design area; callers draw nothing then.
inline float ResolveScreenPixelsPerMeter(int screenWidth, int screenHeight,
                                         const EngineSettings& s = EngineSettings::Global())
{
    if (screenWidth <= 0 || screenHeight <= 0 || s.designWidth <= 0.0f || s.designHeight <= 0.0f)
        return 0.0f;
    const float sx = static_cast<float>(screenWidth) / s.designWidth;
    const float sy = static_cast<float>(screenHeight) / s.designHeight;
    const float fit = (s.screenFit == ScreenFit::Fill) ? (sx > sy ? sx : sy) : (sx < sy ? sx : sy);
    if (!s.pixelPerfect || s.pixelsPerMeter <= 0.0f)
        return fit;
    return s.pixelsPerMeter * static_cast<float>(PixelPerfectMultiple(fit / s.pixelsPerMeter));
}

/// Perspective counterpart of the fit: the vertical field of view (degrees) a
/// screen of this size uses, given the camera's field of view on the design
/// shape. A screen narrower than the design widens it under ShowAll so the
/// design's horizontal view still fits; Fill narrows it on a wider screen so
/// the screen stays covered. Pixel perfect does not apply to perspective.
inline float ResolveVerticalFieldOfView(float designFovDegrees, int screenWidth, int screenHeight,
                                        const EngineSettings& s = EngineSettings::Global())
{
    if (screenWidth <= 0 || screenHeight <= 0 || s.designWidth <= 0.0f || s.designHeight <= 0.0f)
        return designFovDegrees;
    constexpr float kDegToRad = 3.14159265358979f / 180.0f;
    const float designAspect = s.designWidth / s.designHeight;
    const float screenAspect = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);
    // Only a narrower screen under ShowAll, or a wider one under Fill, changes
    // the field of view. Anything else returns it untouched, not through
    // atan(tan(x)), which in float does not come back exactly.
    const bool narrower = screenAspect < designAspect;
    const bool fill = (s.screenFit == ScreenFit::Fill);
    if (narrower == fill || screenAspect == designAspect)
        return designFovDegrees;
    return 2.0f * std::atan(std::tan(designFovDegrees * kDegToRad * 0.5f) * designAspect / screenAspect) / kDegToRad;
}

/// The design area in pixels at the art density: what the editor previews by
/// default, and the resolution at which it is drawn 1:1.
inline void DesignSizePixels(int& width, int& height, const EngineSettings& s = EngineSettings::Global())
{
    width = static_cast<int>(std::lround(s.designWidth * s.pixelsPerMeter));
    height = static_cast<int>(std::lround(s.designHeight * s.pixelsPerMeter));
}

}  // namespace Deki
