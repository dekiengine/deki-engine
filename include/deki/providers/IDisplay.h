#pragma once

#include <stdint.h>
#include <deki/providers/Rect.h>

namespace Deki
{

// Declared here instead of including Engine.h: a display only names the
// format. An opaque enum declaration is enough to pass one by value.
enum class ColorFormat;

/// What a display package implements: setup, presenting frames, events and
/// UI overlays. The platform package loaded at runtime registers its display
/// with Engine::SetDisplay(this, name); the engine holds only the pointer.
class IDisplay
{
public:
    virtual ~IDisplay() = default;

    virtual bool Initialize(int32_t width, int32_t height) = 0;

    virtual void Shutdown() = 0;

    /// Shows the whole framebuffer. Use FrameBufferBytes(format, ...) for its
    /// size rather than working out bytes per pixel.
    virtual void Present(const uint8_t* framebuffer, int width, int height, ColorFormat format) = 0;

    /// True when PresentRegions() sends only the rectangles it is given. A
    /// display that can address part of its panel or texture returns true and
    /// implements PresentRegions; otherwise the engine never calls it.
    virtual bool SupportsPartialPresent() const { return false; }

    /// Shows only `rects` of the framebuffer: framebuffer pixels, half-open,
    /// already clipped, possibly overlapping. `count` 0 means nothing changed
    /// since the last present: a windowed display still redraws its window
    /// from the texture it holds, an LCD does nothing. The default sends the
    /// whole frame.
    virtual void PresentRegions(const uint8_t* framebuffer, int width, int height, ColorFormat format,
                                const Rect* rects, int32_t count)
    {
        (void)rects;
        (void)count;
        Present(framebuffer, width, height, format);
    }

    virtual void GetDisplaySize(int32_t* width, int32_t* height) const = 0;

    /// The pixel format this display takes. With GetDisplaySize, it sets the
    /// framebuffer's size and format when the display is registered, so every
    /// target renders in its own screen's format and Present never converts.
    virtual ColorFormat GetColorFormat() const = 0;

    virtual bool IsInitialized() const = 0;

    /// Makes the next present redraw the whole screen.
    virtual void RequestFullRefresh() = 0;

    /// Handles platform events (window close, resize, ...). Returns false when
    /// the application should exit.
    virtual bool ProcessEvents() = 0;

    // ---- UI overlays, for UI systems ----

    virtual void* CreateUIOverlay(int32_t width, int32_t height) = 0;
    virtual bool UpdateUIOverlay(void* overlay, int32_t x, int32_t y, int32_t width, int32_t height,
                                 const uint32_t* pixels) = 0;
    virtual bool UpdateUIOverlayRGB565A8(void* overlay, int32_t x, int32_t y, int32_t width, int32_t height,
                                         const uint8_t* rgb565a8Pixels) = 0;
    virtual void DestroyUIOverlay(void* overlay) = 0;
    virtual void SetActiveUIOverlay(void* overlay) = 0;
    virtual void ClearActiveUIOverlay() = 0;

    /// A buffer the display owns that the renderer can draw into directly,
    /// such as a DMA-capable buffer in fast internal RAM, saving a copy in
    /// Present. nullptr when there is none.
    virtual uint8_t* GetRenderBuffer(int32_t* width, int32_t* height)
    {
        (void)width;
        (void)height;
        return nullptr;
    }

    /// Turns the backlight on or off. IPower calls it around sleep: off in
    /// OnBeforeSleep so the panel draws no current while the CPU is paused,
    /// on in OnScreenOn after waking. Displays without one (desktop windows)
    /// keep the default, which does nothing.
    virtual void SetBacklight(bool on) { (void)on; }
};

}  // namespace Deki
