#pragma once
#include <cstdint>
#include <deki/providers/Rect.h>

// Forward declaration. The renderer belongs to the deki-rendering package, not
// the engine: this interface only stores one and hands it back, so its name in
// that package's namespace is all the engine needs.
namespace DekiRendering
{
class DekiRenderer;
}

namespace Deki
{

class Scene;
class ICamera;
enum class ColorFormat;

/**
 * @brief Abstract interface for the rendering system
 *
 * Engine uses this interface to interact with the render system
 * without linking to the concrete DekiRenderSystem implementation
 * (which lives in the rendering package DLL).
 */
class IRenderSystem
{
public:
    virtual ~IRenderSystem() = default;

    virtual bool Setup(int32_t width, int32_t height, ColorFormat format) = 0;
    virtual void Render(Scene* current_scene) = 0;

    virtual const uint8_t* GetFrameBuffer() const = 0;
    virtual int32_t GetScreenWidth() const = 0;
    virtual int32_t GetScreenHeight() const = 0;
    virtual ColorFormat GetColorFormat() const = 0;

    virtual void SetRenderer(DekiRendering::DekiRenderer* renderer) = 0;
    virtual DekiRendering::DekiRenderer* GetRenderer() const = 0;

    /**
     * @brief Render a scene to an external buffer (used by edit mode)
     */
    virtual void RenderToBuffer(Scene* scene, ICamera* camera, uint8_t* buffer, int32_t width, int32_t height,
                                ColorFormat format) = 0;

    /**
     * @brief Force the next present to cover the whole framebuffer.
     *
     * For anything that changed pixels the renderer did not draw: a display
     * swap, an overlay shown or hidden, a direct write to the framebuffer.
     */
    virtual void MarkAllDirty() {}

    /**
     * @brief The rectangles the last Render() left needing presentation.
     *
     * Engine::PresentFrame hands them to IDisplay::PresentRegions when
     * the display supports partial presents. *count == -1 means present
     * everything (tracking off, or the frame is unknown); 0 means nothing
     * changed; > 0 is the number of rectangles returned, valid until the next
     * Render(). The default render system reports -1.
     */
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
