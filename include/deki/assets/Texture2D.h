#pragma once

#include <deki/providers/Buffer.h>
#include <cstdint>

// Header::flags
#define DTEX_FLAG_HAS_TRANSPARENCY 0x01  // some pixels are transparent
#define DTEX_FLAG_HAS_ALPHA 0x02         // has an alpha channel
#define DTEX_FLAG_IS_SPRITE 0x04         // the file carries sprite metadata
#define DTEX_FLAG_ALL_OPAQUE 0x08        // every alpha is 255, so no runtime scan

// An image: width, height, format, pixels, and the .dtex header that stores
// them. It lives in core, next to the asset system that loads it and to
// ColorFormat: ColorFormat says what a display scans out, this says what an
// image holds.
//
// It is not in deki-2d because nothing here is 2D (a sprite is: nine-slice
// borders, row spans for QuadBlit). A 3D renderer textures with it, and an
// editor tool can inspect it with no renderer at all.

namespace Deki
{

class Texture2D
{
public:
    enum class TextureFormat : uint32_t
    {
        RGB888 = 0,    // 3 bytes per pixel
        RGBA8888 = 1,  // 4 bytes per pixel
        RGB565 = 2,    // 2 bytes per pixel, for tight memory
        RGB565A8 = 3,  // 3 bytes per pixel (RGB565 + 8-bit alpha)
        ALPHA8 = 4     // 1 byte per pixel (alpha only)
    };
    /// Header of a texture file. The file is [Header][pixel data][metadata].
    struct Header
    {
        char magic[4];     // "2DTX"
        uint32_t version;  // currently 1
        uint32_t width;    // pixels
        uint32_t height;   // pixels
        TextureFormat format;
        uint32_t dataSize;      // bytes of pixel data
        uint32_t metadataSize;  // bytes of metadata, 0 when none
        uint32_t flags;         // DTEX_FLAG_*
    };

    uint8_t* data;  // pixels, alpha included when the format has it; runtime only
    int32_t width;
    int32_t height;
    TextureFormat format;

    bool hasTransparency;  // some pixels are transparent
    bool hasAlpha;         // has an alpha channel

    // Per-row opaque spans, for RGB565A8 sprites with alpha. For each row,
    // opaqueStart is the first fully opaque column and opaqueEnd one past the
    // last. Pixels outside [opaqueStart, opaqueEnd) need blending; pixels
    // inside are written directly. Empty when not computed (other formats, or
    // fully opaque or fully transparent sprites). Owning: allocate it only
    // through this Buffer, never new[].
    Deki::Buffer<int16_t> alphaRowSpans;
#ifdef DEKI_EDITOR
    bool allocatedWithBackend;  // which allocator was used: the backend in play mode, malloc in edit mode
#endif

    Texture2D();
    virtual ~Texture2D();

    /// Loads a compiled texture file (.tex), or returns nullptr.
    static Texture2D* Load(const char* filePath);

    static uint32_t GetBytesPerPixel(TextureFormat format);

    /// The format's name, for logs.
    static const char* GetFormatName(TextureFormat format);

    /// True when the header is well formed.
    static bool ValidateHeader(const Texture2D::Header& header);

#ifdef DEKI_EDITOR
    /// Loads a .tex file as RGBA8888 pixels (editor). The caller owns the
    /// result and frees it with delete[]; nullptr on failure.
    static uint8_t* LoadAsRGBA(const char* filePath, int32_t& outWidth, int32_t& outHeight, bool& outHasAlpha);

    /// Converts pixels in `format` to RGBA8888. The caller owns the result and
    /// frees it with delete[]; nullptr on failure.
    static uint8_t* ConvertToRGBA(const uint8_t* srcData, int32_t width, int32_t height, TextureFormat format);
#endif

protected:
    /// Fills the texture from `data`, the file's bytes after `header`.
    /// Returns false on failure.
    virtual bool LoadFromMemory(const Texture2D::Header& header, const uint8_t* data);
};

}  // namespace Deki
