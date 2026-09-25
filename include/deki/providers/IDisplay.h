#pragma once

#include <stdint.h>
#include <deki/providers/Rect.h>

/**
 * @brief Abstract interface for display package operations
 *
 * This interface defines the contract that display packages must implement
 * to work with the Deki engine. It abstracts display initialization,
 * buffer management, and rendering operations.
 *
 * Whichever platform integration package is loaded at runtime registers its
 * concrete display via Engine::SetDisplay(this, name). Engine-core owns
 * no concrete display logic - only the IDisplay* pointer and the
 * lifecycle coordination around it.
 */

namespace Deki
{

// Forward-declared rather than including Engine.h: a display needs to name the
// format, not to know what an Engine is. The underlying type is fixed by the
// definition, so an opaque declaration is enough to pass one by value.
enum class ColorFormat;

class IDisplay
{
public:
    virtual ~IDisplay() = default;

    /**
     * @brief Initialize the platform display
     */
    virtual bool Initialize(int32_t width, int32_t height) = 0;

    /**
     * @brief Shutdown the display and cleanup resources
     */
    virtual void Shutdown() = 0;

    /**
     * @brief Present the frame buffer to the display
     *
     * `format` used to be an int, with the enum's numbering repeated in a
     * comment here and as literals in every display package. That comment was
     * already missing RGB565A8, and one package computed bytes-per-pixel from
     * the literals in a way that gave it 2 instead of 3. Passing the enum makes
     * the boundary type-checked and lets a compiler flag a format nobody
     * handles.
     */
    virtual void Present(const uint8_t* framebuffer, int width, int height,
                         ColorFormat format) = 0;

    /**
     * @brief True when PresentRegions() pushes only the rectangles it is given.
     *
     * The default display presents whole frames, and the engine then never
     * calls PresentRegions. A display that can address a sub-rectangle of its
     * panel or texture returns true and implements PresentRegions.
     */
    virtual bool SupportsPartialPresent() const { return false; }

    /**
     * @brief Present only `rects` of the framebuffer.
     *
     * Rectangles are framebuffer pixels, half-open, already clipped to the
     * buffer; they may overlap. `count` 0 means nothing changed since the last
     * present: a windowed display still redraws its window from the texture it
     * holds, an LCD does nothing. The default pushes the whole frame, so a
     * display that does not override this behaves exactly as before.
     */
    virtual void PresentRegions(const uint8_t* framebuffer, int width, int height,
                                ColorFormat format, const Rect* rects, int32_t count)
    {
        (void)rects;
        (void)count;
        Present(framebuffer, width, height, format);
    }

    /**
     * @brief Get the display dimensions
     */
    virtual void GetDisplaySize(int32_t* width, int32_t* height) const = 0;

    /**
     * @brief The pixel format this display takes.
     *
     * With GetDisplaySize, what the engine sizes and formats the framebuffer
     * to when the display is set: every target renders in its own screen's
     * format, so Present never has to convert.
     */
    virtual ColorFormat GetColorFormat() const = 0;

    /**
     * @brief Check if the display is initialized
     */
    virtual bool IsInitialized() const = 0;

    /**
     * @brief Request a full display refresh
     */
    virtual void RequestFullRefresh() = 0;

    /**
     * @brief Process platform events (window close, resize, etc.)
     * @return true if application should continue running, false to exit
     */
    virtual bool ProcessEvents() = 0;

    // ---- UI / Overlay support for UI systems ----

    virtual void* CreateUIOverlay(int32_t width, int32_t height) = 0;
    virtual bool UpdateUIOverlay(
        void* overlay, int32_t x, int32_t y, int32_t width, int32_t height, const uint32_t* pixels) = 0;
    virtual bool UpdateUIOverlayRGB565A8(
        void* overlay, int32_t x, int32_t y, int32_t width, int32_t height, const uint8_t* rgb565a8_pixels) = 0;
    virtual void DestroyUIOverlay(void* overlay) = 0;
    virtual void SetActiveUIOverlay(void* overlay) = 0;
    virtual void ClearActiveUIOverlay() = 0;

    /**
     * @brief Get a display-owned buffer suitable for direct rendering
     *
     * If the display has an internal DMA-aligned buffer (e.g. in fast internal RAM),
     * DekiRenderSystem can render directly into it, avoiding a memcpy during Present.
     */
    virtual uint8_t* GetRenderBuffer(int32_t* width, int32_t* height)
    {
        (void)width;
        (void)height;
        return nullptr;
    }

    /**
     * @brief Turn the display backlight on or off.
     *
     * Called by IPower around sleep transitions: off in OnBeforeSleep so the
     * panel doesn't burn current while the CPU is paused, on in OnScreenOn after
     * wake. Displays without controllable backlight (desktop windows) leave the
     * default no-op.
     */
    virtual void SetBacklight(bool on) { (void)on; }
};

}  // namespace Deki
