#pragma once

#include <cstdint>
#include <deki/Engine.h>
#include <deki/ICamera.h>  // UnitMode
#include <deki/reflection/Property.h>

/**
 * @brief Project-wide unit settings (inspector display preference, world scale).
 *
 * Registered with SettingsRegistry under the "Units" section. Internal
 * storage is always meters; this struct only configures inspector display
 * conversion and seeds default camera zoom.
 *
 * Hot paths (inspector, scene view, cameras) read these values from a
 * per-frame cache, EngineSettings::Global(). Call ApplyToGlobalCache()
 * after Project::Load and after any Project Settings edit so the cache picks
 * up the new values without a project reopen.
 */

namespace Deki
{

DEKI_CATEGORY("Settings")
DEKI_SETTINGS_SECTION("Units")
class UnitProjectSettings : public Component
{
public:

    DEKI_TOOLTIP("Inspector display preference. Pixels: physical-quantity fields show as px / px/s / px/s². Meters: same fields show as m / m/s / m/s². Internal storage is always meters either way.")
    DEKI_EXPORT
    UnitMode inspectorUnit = UnitMode::Pixels;

    DEKI_TOOLTIP("World scale: how many framebuffer pixels render one meter of world space. Drives inspector px<->m conversion and the default zoom of new cameras (CameraComponent::pixelsPerMeter sentinel).")
    DEKI_RANGE(1.0f, 1024.0f)
    DEKI_EXPORT
    float pixelsPerMeter = 16.0f;

    void ApplyToGlobalCache() const
    {
        EngineSettings::Global().inspectorUnit = inspectorUnit;
        EngineSettings::Global().pixelsPerMeter = pixelsPerMeter;
    }
};


}  // namespace Deki
