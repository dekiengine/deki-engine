#pragma once

#include <cstdint>
#include <deki/Engine.h>
#include <deki/reflection/Property.h>

/**
 * @brief Engine-core project settings that are not tied to a specific subsystem.
 *
 * Registered under the "Core" section. Frame pacing lives here rather than with
 * the design area in "Framebuffer": capping is a property of the engine's frame
 * loop. Nothing passes these to a renderer, and they still apply with no
 * rendering package installed at all.
 */

namespace Deki
{

DEKI_CATEGORY("Settings")
DEKI_SETTINGS_SECTION("Core")
class CoreProjectSettings : public Component
{
public:

    DEKI_TOOLTIP("Cap play-mode FPS at the target frame rate below. Disable to let the game run at the editor's refresh rate (typically 60 Hz with vsync).")
    DEKI_EXPORT
    bool capFps = true;

    DEKI_TOOLTIP("Target frame rate (FPS) when capping is enabled. The game tries to hit this rate; if the hardware can't keep up it runs slower. Ignored when 'Cap FPS' is off.")
    DEKI_RANGE(1, 240)
    DEKI_EXPORT
    int32_t targetFps = 60;
};


}  // namespace Deki
