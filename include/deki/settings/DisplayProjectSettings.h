#pragma once

#include <cstdint>
#include <deki/Engine.h>
#include <deki/reflection/Property.h>

/**
 * @brief The framebuffer contract: logical render resolution and color format.
 *
 * Registered under the "Framebuffer" section — this is exactly the triple passed
 * to IRenderSystem::Setup(width, height, format), so it is renderer-agnostic
 * and dictated by the target hardware rather than chosen by a renderer. The editor's
 * Project Settings panel renders these fields via reflection; persistence goes
 * through Project::Save/Load (deki.json) and ProjectSettings::Write/Load
 * (dproject.bin) for the runtime device.
 *
 * targetWidth / targetHeight is the **logical resolution** — what the engine
 * renders into. On embedded the panel size matches this exactly. On desktop
 * preview, SDL3 handles the integer-multiple upscale via
 * SDL_SetRenderLogicalPresentation with nearest-neighbor sampling.
 */

namespace Deki
{

DEKI_CATEGORY("Settings")
DEKI_SETTINGS_SECTION("Framebuffer")
class DisplayProjectSettings : public Component
{
public:

    DEKI_TOOLTIP("Logical render width in pixels. The engine renders into this resolution; the device or desktop preview upscales to the actual display.")
    DEKI_RANGE(1, 4096)
    DEKI_EXPORT
    int32_t targetWidth = 320;

    DEKI_TOOLTIP("Logical render height in pixels. The engine renders into this resolution; the device or desktop preview upscales to the actual display.")
    DEKI_RANGE(1, 4096)
    DEKI_EXPORT
    int32_t targetHeight = 240;

    DEKI_TOOLTIP("Framebuffer pixel format. RGB565 (default) is the smallest and fastest — typical for embedded displays. RGB888 gives full 8-bit color but uses 50% more memory. ARGB8888 adds an alpha channel and uses 100% more memory.")
    DEKI_EXPORT
    ColorFormat colorFormat = ColorFormat::RGB565;
};


}  // namespace Deki
