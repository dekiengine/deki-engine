#pragma once

#include <cstdint>
#include <deki/Engine.h>
#include <deki/reflection/Property.h>

/**
 * @brief The design area: how much world the player sees, on any screen.
 *
 * Registered under the "Framebuffer" section (the name is the settings file,
 * settings/framebuffer.json, so it stays). Every target renders at its own
 * display's native size and pixel format; nothing here sizes or formats a
 * buffer. The design area is in
 * meters, so the inspector shows it in px or m like any other length: 80 x 45 m
 * at 16 px/m reads as 1280 x 720 px.
 *
 * ApplyToGlobalCache() copies the values into EngineSettings::Global(), which
 * the cameras read through ResolveScreenPixelsPerMeter (ScreenScale.h). The
 * device reads them back from dproject.bin's settings chunk.
 */

namespace Deki
{

DEKI_CATEGORY("Settings")
DEKI_SETTINGS_SECTION("Framebuffer")
class DisplayProjectSettings : public Component
{
public:

    DEKI_TOOLTIP("Width of the design area: how much world the player sees across. Every screen shows this much or more; see Screen Fit.")
    DEKI_UNIT(Distance)
    DEKI_RANGE(0.01f, 100000.0f)
    DEKI_EXPORT
    float designWidth = 20.0f;

    DEKI_TOOLTIP("Height of the design area: how much world the player sees top to bottom. Every screen shows this much or more; see Screen Fit.")
    DEKI_UNIT(Distance)
    DEKI_RANGE(0.01f, 100000.0f)
    DEKI_EXPORT
    float designHeight = 15.0f;

    DEKI_TOOLTIP("What a screen of another shape does. Show All keeps the whole design area visible and shows extra world on the longer side. Fill covers the screen and crops the design area's edges.")
    DEKI_EXPORT
    ScreenFit screenFit = ScreenFit::ShowAll;

    DEKI_TOOLTIP("For pixel art. Scales by whole numbers only, so every art pixel is the same size on screen, and snaps positions to the art-pixel grid. Screens smaller than the design area stay at 1x and crop.")
    DEKI_EXPORT
    bool pixelPerfect = false;

    void ApplyToGlobalCache() const
    {
        EngineSettings::Global().designWidth = designWidth;
        EngineSettings::Global().designHeight = designHeight;
        EngineSettings::Global().screenFit = screenFit;
        EngineSettings::Global().pixelPerfect = pixelPerfect;
    }
};


}  // namespace Deki
