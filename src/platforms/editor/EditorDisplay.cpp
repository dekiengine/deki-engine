#ifdef DEKI_EDITOR

#include <deki/Engine.h>  // ColorFormat, FrameBufferBytes
#include "EditorDisplay.h"
#include <deki/LogSystem.h>

#include <cstring>  // memcpy - pulled in transitively by <windows.h> on MSVC only

// OpenGL headers
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>
#ifndef GL_UNSIGNED_SHORT_5_6_5
#define GL_UNSIGNED_SHORT_5_6_5 0x8363  // GL 1.2; the Windows SDK header stops at 1.1
#endif

namespace Deki
{


EditorDisplay::EditorDisplay()
    : m_GlTextureId(0), m_RgbaBuffer(nullptr), m_DisplayWidth(0), m_DisplayHeight(0), initialized(false)
{
}

namespace
{
// One row of the framebuffer to RGBA for GL. RGB565 never comes here: GL takes
// it as it is. ARGB8888 is a 0xAARRGGBB word per pixel, so its bytes in memory
// are B, G, R, A; RGB565A8 is [565 lo, 565 hi, coverage], shown opaque.
void RowToRgba(ColorFormat format, const uint8_t* src, uint8_t* dst, int32_t count)
{
    for (int32_t x = 0; x < count; ++x)
    {
        uint8_t* d = dst + x * 4;
        switch (format)
        {
            case ColorFormat::RGB888:
                d[0] = src[x * 3 + 0];
                d[1] = src[x * 3 + 1];
                d[2] = src[x * 3 + 2];
                break;
            case ColorFormat::ARGB8888:
                d[0] = src[x * 4 + 2];
                d[1] = src[x * 4 + 1];
                d[2] = src[x * 4 + 0];
                break;
            case ColorFormat::RGB565A8:
            default:
            {
                const uint16_t v = static_cast<uint16_t>(src[x * 3] | (src[x * 3 + 1] << 8));
                const uint8_t r5 = (v >> 11) & 0x1F, g6 = (v >> 5) & 0x3F, b5 = v & 0x1F;
                d[0] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
                d[1] = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
                d[2] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
                break;
            }
        }
        d[3] = 255;
    }
}
}  // namespace

EditorDisplay::~EditorDisplay()
{
    Shutdown();
}

bool EditorDisplay::Initialize(int32_t width, int32_t height)
{
    if (initialized)
    {
        return true;
    }

    m_DisplayWidth = width;
    m_DisplayHeight = height;

    // Allocate single RGBA buffer (no double-buffering needed for ImGui)
    m_RgbaBuffer = new uint8_t[width * height * 4];

    // Create single OpenGL texture
    GLuint texId;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_GlTextureId = texId;
    glBindTexture(GL_TEXTURE_2D, 0);

    initialized = true;

    DEKI_LOG_INTERNAL("EditorDisplay initialized: %dx%d", width, height);
    return true;
}

void EditorDisplay::Shutdown()
{
    if (!initialized)
    {
        return;
    }

    // Delete texture
    if (m_GlTextureId != 0)
    {
        GLuint texId = m_GlTextureId;
        glDeleteTextures(1, &texId);
        m_GlTextureId = 0;
    }

    // Delete buffer
    delete[] m_RgbaBuffer;
    m_RgbaBuffer = nullptr;

    m_DisplayWidth = 0;
    m_DisplayHeight = 0;
    initialized = false;

    DEKI_LOG_INTERNAL("EditorDisplay shutdown");
}

void EditorDisplay::Present(const uint8_t* framebuffer, int width, int height, ColorFormat format)
{
    if (!initialized || !framebuffer)
    {
        return;
    }

    uint8_t* dst = m_RgbaBuffer;

    // RGB565 is handed to GL as-is (GL_UNSIGNED_SHORT_5_6_5): the per-pixel
    // conversion that ran here every frame at the game's resolution is gone.
    if (format == ColorFormat::RGB565)  // RGB565
    {
        glBindTexture(GL_TEXTURE_2D, m_GlTextureId);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, framebuffer);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glBindTexture(GL_TEXTURE_2D, 0);
        return;
    }
    RowToRgba(format, framebuffer, dst, width * height);

    // Upload to texture
    glBindTexture(GL_TEXTURE_2D, m_GlTextureId);
    // Unpack state is global; other GL users (e.g. ImGui glyph uploads) can leave
    // a row stride behind, which would shear/overread this tightly packed upload.
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, dst);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void EditorDisplay::PresentRegions(const uint8_t* framebuffer, int width, int height, ColorFormat format,
                                   const Rect* rects, int32_t count)
{
    if (!initialized || !framebuffer || count <= 0)
        return;  // nothing changed: the texture already shows the frame

    glBindTexture(GL_TEXTURE_2D, m_GlTextureId);
    // Upload each rectangle straight out of the framebuffer: the row length
    // tells GL the source pitch, the pointer is offset to the rectangle.
    glPixelStorei(GL_UNPACK_ROW_LENGTH, width);

    if (format == ColorFormat::RGB565)  // RGB565, handed to GL as-is
    {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
        for (int32_t i = 0; i < count; ++i)
        {
            const Rect& r = rects[i];
            if (r.Empty()) continue;
            const uint8_t* src = framebuffer + (static_cast<size_t>(r.top) * width + r.left) * 2;
            glTexSubImage2D(GL_TEXTURE_2D, 0, r.left, r.top, r.Width(), r.Height(),
                            GL_RGB, GL_UNSIGNED_SHORT_5_6_5, src);
        }
    }
    else
    {
        // Convert only the rectangle's rows into the RGBA staging buffer (same
        // pitch as the framebuffer) and upload from there.
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        for (int32_t i = 0; i < count; ++i)
        {
            const Rect& r = rects[i];
            if (r.Empty()) continue;
            for (int32_t y = r.top; y < r.bottom; ++y)
            {
                uint8_t* dst = m_RgbaBuffer + (static_cast<size_t>(y) * width + r.left) * 4;
                const size_t bpp = FrameBufferBytes(format, 1, 1);
                RowToRgba(format, framebuffer + (static_cast<size_t>(y) * width + r.left) * bpp, dst, r.Width());
            }
            const uint8_t* src = m_RgbaBuffer + (static_cast<size_t>(r.top) * width + r.left) * 4;
            glTexSubImage2D(GL_TEXTURE_2D, 0, r.left, r.top, r.Width(), r.Height(),
                            GL_RGBA, GL_UNSIGNED_BYTE, src);
        }
    }

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void EditorDisplay::GetDisplaySize(int32_t* width, int32_t* height) const
{
    if (width) *width = m_DisplayWidth;
    if (height) *height = m_DisplayHeight;
}

bool EditorDisplay::IsInitialized() const
{
    return initialized;
}

void EditorDisplay::RequestFullRefresh()
{
    // No-op for editor - always full refresh
}

bool EditorDisplay::ProcessEvents()
{
    // Editor handles its own events via SDL3/ImGui
    return true;
}

void* EditorDisplay::CreateUIOverlay(int32_t width, int32_t height)
{
    // Not used in editor - ImGui handles overlays
    (void)width;
    (void)height;
    return nullptr;
}

bool EditorDisplay::UpdateUIOverlay(void* overlay, int32_t x, int32_t y,
                                    int32_t width, int32_t height, const uint32_t* pixels)
{
    (void)overlay;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)pixels;
    return false;
}

bool EditorDisplay::UpdateUIOverlayRGB565A8(void* overlay, int32_t x, int32_t y,
                                            int32_t width, int32_t height, const uint8_t* rgb565a8_pixels)
{
    (void)overlay;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)rgb565a8_pixels;
    return false;
}

void EditorDisplay::DestroyUIOverlay(void* overlay)
{
    (void)overlay;
}

void EditorDisplay::SetActiveUIOverlay(void* overlay)
{
    (void)overlay;
}

void EditorDisplay::ClearActiveUIOverlay()
{
    // No-op
}

}  // namespace Deki

#endif  // DEKI_EDITOR
