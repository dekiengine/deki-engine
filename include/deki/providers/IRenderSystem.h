#pragma once
#include <cstdint>
#include <deki/providers/Rect.h>

// The renderer belongs to the deki-rendering package. This interface only
// stores one and hands it back, so a declaration is all the engine needs.
namespace DekiRendering
{
class DekiRenderer;
}

namespace Deki
{

class Scene;
class ICamera;
enum class ColorFormat;

/// The render system as the engine sees it, so it need not link to the
/// implementation in the rendering package DLL.
class IRenderSystem
{
public:
    virtual ~IRenderSystem() = default;

    virtual bool Setup(int32_t width, int32_t height, ColorFormat format) = 0;
    virtual void Render(Scene* currentScene) = 0;

    virtual const uint8_t* GetFrameBuffer() const = 0;
    virtual int32_t GetScreenWidth() const = 0;
    virtual int32_t GetScreenHeight() const = 0;
    virtual ColorFormat GetColorFormat() const = 0;

    virtual void SetRenderer(DekiRendering::DekiRenderer* renderer) = 0;
    virtual DekiRendering::DekiRenderer* GetRenderer() const = 0;

    /// Renders `scene` through `camera` into a caller's buffer (edit mode).
    virtual void RenderToBuffer(Scene* scene, ICamera* camera, uint8_t* buffer, int32_t width, int32_t height,
                                ColorFormat format) = 0;

    /// Makes the next present cover the whole framebuffer. Call it after
    /// pixels change that the renderer did not draw: a display swap, an
    /// overlay shown or hidden, a direct write to the framebuffer.
    virtual void MarkAllDirty() {}

    /// The rectangles the last Render() changed, which Engine::PresentFrame
    /// passes to IDisplay::PresentRegions when the display supports partial
    /// presents. *count -1 means present everything (tracking off, or the
    /// frame is unknown); 0 means nothing changed; above 0 it is the number of
    /// rectangles, valid until the next Render(). The default reports -1.
    virtual const Rect* GetPresentRects(int32_t* count) const
    {
        if (count)
        {
            *count = -1;
        }
        return nullptr;
    }
};

}  // namespace Deki
