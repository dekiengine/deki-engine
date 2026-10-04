#pragma once

#ifdef DEKI_EDITOR
#include <deki/providers/IDisplay.h>
#include <cstdint>
#endif

/**
 * @brief Editor implementation of display interface
 *
 * This display renders to an OpenGL texture that can be displayed in ImGui.
 * It doesn't create its own window - the editor manages the window.
 */

namespace Deki
{

#ifdef DEKI_EDITOR

class EditorDisplay : public IDisplay
{
private:
    uint32_t m_GlTextureId;  // Single texture (no double-buffering needed for ImGui)
    uint8_t* m_RgbaBuffer;   // Single staging buffer

    int32_t m_DisplayWidth;
    int32_t m_DisplayHeight;
    ColorFormat m_Format = ColorFormat::RGB565;
    bool m_Initialized;

public:
    EditorDisplay();
    virtual ~EditorDisplay();

    // IDisplay interface
    bool Initialize(int32_t width, int32_t height) override;
    void Shutdown() override;
    void Present(const uint8_t* framebuffer, int width, int height, ColorFormat format) override;
    bool SupportsPartialPresent() const override { return true; }
    void PresentRegions(const uint8_t* framebuffer, int width, int height, ColorFormat format, const Rect* rects,
                        int32_t count) override;
    void GetDisplaySize(int32_t* width, int32_t* height) const override;
    ColorFormat GetColorFormat() const override { return m_Format; }
    bool IsInitialized() const override;
    void RequestFullRefresh() override;
    bool ProcessEvents() override;

    // UI/Overlay support (not used in editor - ImGui handles overlays)
    void* CreateUIOverlay(int32_t width, int32_t height) override;
    bool UpdateUIOverlay(void* overlay, int32_t x, int32_t y, int32_t width, int32_t height,
                         const uint32_t* pixels) override;
    bool UpdateUIOverlayRGB565A8(void* overlay, int32_t x, int32_t y, int32_t width, int32_t height,
                                 const uint8_t* rgb565a8Pixels) override;
    void DestroyUIOverlay(void* overlay) override;
    void SetActiveUIOverlay(void* overlay) override;
    void ClearActiveUIOverlay() override;

    // Editor-specific methods
    uint32_t GetTextureId() const { return m_GlTextureId; }

    // The format the play view previews in: the screen being emulated. Set
    // before Initialize; the engine formats the framebuffer to it.
    void SetColorFormat(ColorFormat format) { m_Format = format; }
};

#endif  // DEKI_EDITOR

}  // namespace Deki
