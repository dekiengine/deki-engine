#pragma once

#include <deki/providers/Buffer.h>
#include <cstdint>

// DTEX format flags
#define DTEX_FLAG_HAS_TRANSPARENCY 0x01  // Image has transparent pixels
#define DTEX_FLAG_HAS_ALPHA 0x02         // Image has alpha channel
#define DTEX_FLAG_IS_SPRITE 0x04         // File contains sprite metadata
#define DTEX_FLAG_ALL_OPAQUE 0x08        // All alpha pixels are 255 (skip runtime scan)

/**
 * @brief Base texture class for V-Engine
 */
// An image: width, height, format, pixels, and the .dtex header that stores
// them. In core beside the asset system that loads it, and beside ColorFormat
// -- one says what a display scans out, this says what an image holds.
//
// Not in deki-2d, because nothing here is 2D. A sprite is: it carries
// nine-slice borders and the chroma and alpha row spans QuadBlit fast-paths.
// A texture is an image, which a 3D renderer wants the moment it textures
// anything, and which an editor tool can inspect with no renderer at all --
// this file includes Buffer, Memory, FileSystem and LogSystem, and nothing
// else.

namespace Deki
{

class Texture2D
{
   public:
    /**
     * @brief Texture formats supported by V-Engine
     */
    enum class TextureFormat : uint32_t
    {
        RGB888 = 0,  // 3 bytes per pixel
        RGBA8888 = 1,  // 4 bytes per pixel
        RGB565 = 2,  // 2 bytes per pixel (for memory constrained scenarios)
        RGB565A8 = 3,  // 3 bytes per pixel (RGB565 + 8-bit alpha)
        ALPHA8 = 4  // 1 byte per pixel (alpha only)
    };
    /**
     * @brief V-Engine Texture Binary Format Header
     *
     * File structure:
     * [Header][pixel_data][metadata]
     */
    struct Header
    {
        char magic[4];  // Magic number "2DTX"
        uint32_t version;  // Format version (currently 1)
        uint32_t width;  // Texture width in pixels
        uint32_t height;  // Texture height in pixels
        TextureFormat format;  // Pixel format
        uint32_t dataSize;  // Size of pixel data in bytes
        uint32_t metadataSize;  // Size of additional metadata (0 if none)
        uint32_t flags;  // Format flags (DTEX_FLAG_*)
    };

    uint8_t* data;  // Pixel data (includes alpha if format has it) - Runtime only
    int32_t width;  // Texture width
    int32_t height;  // Texture height
    TextureFormat format;  // Pixel format

    bool hasTransparency;  // Whether the texture has transparent pixels
    bool hasAlpha;  // Whether the texture has an alpha channel

    // Per-row opaque span data for RGB565A8 sprites with alpha.
    // For each row: opaqueStart = first fully opaque column, opaqueEnd = last+1 fully opaque column.
    // Pixels outside [opaqueStart, opaqueEnd) have alpha and need blending.
    // Pixels inside this range are fully opaque and can be written directly (no blend math).
    // nullptr if not computed (non-RGB565A8, or fully opaque/transparent sprites).
    // Owning. This member is where the mismatch was: loaders allocated it
    // with new[] in one place, Deki::Memory in another, and nothing at all
    // on a third path, while the destructor always used delete[].
    Deki::Buffer<int16_t> alphaRowSpans;
#ifdef DEKI_EDITOR
    bool allocatedWithBackend;  // Editor-only: Track which allocator was used (play mode uses backend, edit mode uses malloc)
#endif

    Texture2D();
    virtual ~Texture2D();

    /**
     * @brief Load texture from V-Engine texture format (.vtex)
     * @param file_path Path to the .vtex file
     * @return Pointer to loaded texture or nullptr on failure
     */
    static Texture2D* Load(const char* file_path);

    /**
     * @brief Get bytes per pixel for a format
     * @param format The texture format
     * @return Number of bytes per pixel
     */
    static uint32_t GetBytesPerPixel(TextureFormat format);

    /**
     * @brief Get format name as string (for debugging)
     * @param format The texture format
     * @return Format name
     */
    static const char* GetFormatName(TextureFormat format);

    /**
     * @brief Validate texture header
     * @param header Header to validate
     * @return true if header is valid
     */
    static bool ValidateHeader(const Texture2D::Header& header);

#ifdef DEKI_EDITOR
    /**
     * @brief Load texture from file and return as RGBA8888 pixel data (for editor)
     * @param file_path Path to the .tex file
     * @param out_width Output width
     * @param out_height Output height
     * @param out_has_alpha Output whether texture has alpha
     * @return RGBA8888 pixel data (caller owns memory, use delete[]), or nullptr on failure
     */
    static uint8_t* LoadAsRGBA(const char* file_path, int32_t& out_width, int32_t& out_height, bool& out_has_alpha);

    /**
     * @brief Convert texture pixel data to RGBA8888 format
     * @param src_data Source pixel data
     * @param width Texture width
     * @param height Texture height
     * @param format Source format
     * @return RGBA8888 pixel data (caller owns memory, use delete[]), or nullptr on failure
     */
    static uint8_t* ConvertToRGBA(const uint8_t* src_data, int32_t width, int32_t height, TextureFormat format);
#endif

   protected:
    /**
     * @brief Load texture data from memory buffer
     * @param header Parsed texture header
     * @param data Raw file data after header
     * @return true on success
     */
    virtual bool LoadFromMemory(const Texture2D::Header& header, const uint8_t* data);
};

}  // namespace Deki
