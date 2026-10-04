#pragma once

#include <cstdint>
#include <deki/Engine.h>
#include <deki/ICamera.h>  // UnitMode
#include <deki/reflection/Property.h>

// Project-wide unit settings, in the "Units" section: the inspector's display
// units and the world scale.
//
// Values are always stored in meters; these settings set the inspector's
// display conversion and the default camera zoom.
//
// Hot paths (inspector, scene view, cameras) read them from a cache,
// EngineSettings::Global(). Call ApplyToGlobalCache() after Project::Load and
// after any Project Settings edit to update it.

namespace Deki
{

DEKI_CATEGORY("Settings")
DEKI_SETTINGS_SECTION("Units")
class UnitProjectSettings : public Component
{
public:
    DEKI_TOOLTIP("Inspector display preference. Pixels: physical-quantity fields show as px / px/s / px/s². Meters: "
                 "same fields show as m / m/s / m/s². Internal storage is always meters either way.")
    DEKI_EXPORT
    UnitMode inspectorUnit = UnitMode::Pixels;

    DEKI_TOOLTIP("World scale: how many framebuffer pixels render one meter of world space. Drives inspector px<->m "
                 "conversion and the default zoom of new cameras (CameraComponent::pixelsPerMeter sentinel).")
    DEKI_RANGE(1.0f, 1024.0f)
    DEKI_EXPORT
    float pixelsPerMeter = 16.0f;

    DEKI_TOOLTIP("Temperature scale shown in the inspector. Storage is always degrees Celsius; this only changes what "
                 "you read and type.")
    DEKI_EXPORT
    TemperatureUnit temperatureUnit = TemperatureUnit::Celsius;

    void ApplyToGlobalCache() const
    {
        EngineSettings::Global().inspectorUnit = inspectorUnit;
        EngineSettings::Global().pixelsPerMeter = pixelsPerMeter;
        EngineSettings::Global().temperatureUnit = temperatureUnit;
    }
};

}  // namespace Deki
