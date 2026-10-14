#include <deki/assets/Texture2D.h>
#include <deki/providers/Memory.h>
#ifndef DEKI_EDITOR
#include <deki/providers/FileSystem.h>
#else
#include <cstdlib>
#include <fstream>
#endif
#include <deki/LogSystem.h>
#include <deki/Engine.h>  // kRGB565BigEndian
#include <cstring>

namespace Deki
{

#ifndef DEKI_EDITOR
namespace
{
// A .dtex keeps RGB565 low byte first; a big-endian board draws from it in its
// own order (kRGB565BigEndian), so the pixels are swapped once on load.
void PixelsToMemoryOrder(const Texture2D::Header& header, uint8_t* pixels)
{
    if constexpr (kRGB565BigEndian)
    {
        if (header.format != Texture2D::TextureFormat::RGB565 && header.format != Texture2D::TextureFormat::RGB565A8)
        {
            return;
        }
        const size_t bpp = header.format == Texture2D::TextureFormat::RGB565 ? 2 : 3;
        const uint64_t count = uint64_t(header.width) * uint64_t(header.height);
        if (count * bpp > header.dataSize)
        {
            return;
        }
        for (uint64_t i = 0; i < count; ++i, pixels += bpp)
        {
            const uint8_t lo = pixels[0];
            pixels[0] = pixels[1];
            pixels[1] = lo;
        }
    }
    else
    {
        (void)header;
        (void)pixels;
    }
}
}  // namespace
#endif

Texture2D::Texture2D()
    : data(nullptr),
      width(0),
      height(0),
      format(Texture2D::TextureFormat::RGB565),
      hasTransparency(false),
      hasAlpha(false)
#ifdef DEKI_EDITOR
      ,
      allocatedWithBackend(false)
#endif
{
}

Texture2D::~Texture2D()
{
    if (data)
    {
#ifdef DEKI_EDITOR
        // Editor: Free using the same allocator that was used for allocation
        // Play mode uses Deki::Memory, edit mode uses std::free
        if (allocatedWithBackend)
        {
            Deki::Memory::Free(data);
        }
        else
        {
            std::free(data);
        }
#else
        // Runtime: Always use Deki::Memory
        Deki::Memory::Free(data);
#endif
        data = nullptr;
    }
    // alphaRowSpans frees itself.
}

#ifndef DEKI_EDITOR
Texture2D* Texture2D::Load(const char* file_path)
{
    if (!file_path)
    {
        DEKI_LOG_ERROR("NULL file path");
        return nullptr;
    }

    Deki::IFileSystem* fs = Deki::FileSystem::GetFileSystemForPath(file_path);
    if (!fs)
    {
        DEKI_LOG_INTERNAL("FileSystem not initialized for path: %s", file_path);
        return nullptr;
    }

    Deki::IFileSystem::FileHandle file = fs->OpenFile(file_path, Deki::IFileSystem::OpenMode::ReadBinary);
    if (!file)
    {
        DEKI_LOG_ERROR("Failed to open texture file: %s", file_path);
        return nullptr;
    }

    long file_size = fs->GetFileSize(file);
    if (file_size < sizeof(Texture2D::Header))
    {
        DEKI_LOG_ERROR("File too small to contain texture header: %s", file_path);
        fs->CloseFile(file);
        return nullptr;
    }

    Texture2D::Header header;
    size_t bytes_read = fs->ReadFile(file, &header, sizeof(Texture2D::Header));
    if (bytes_read != sizeof(Texture2D::Header))
    {
        DEKI_LOG_ERROR("Failed to read texture header: %s", file_path);
        fs->CloseFile(file);
        return nullptr;
    }

    if (!ValidateHeader(header))
    {
        DEKI_LOG_ERROR("Invalid texture header: %s", file_path);
        fs->CloseFile(file);
        return nullptr;
    }

    // Validate file size in 64 bits, so on a 32-bit board a corrupt dataSize
    // plus metadataSize cannot wrap to a small number and pass.
    const uint64_t expected_size =
        uint64_t(sizeof(Texture2D::Header)) + uint64_t(header.dataSize) + uint64_t(header.metadataSize);
    if (file_size < 0 || uint64_t(file_size) < expected_size)
    {
        DEKI_LOG_ERROR("File size mismatch. Expected: %llu, Got: %ld", (unsigned long long)expected_size, file_size);
        fs->CloseFile(file);
        return nullptr;
    }

    // Read pixel data
    uint8_t* pixelData = (uint8_t*)Deki::Memory::Allocate(header.dataSize, Deki::Memory::External);

    if (!pixelData)
    {
        DEKI_LOG_ERROR("Failed to allocate memory for texture data");
        fs->CloseFile(file);
        return nullptr;
    }

    bytes_read = fs->ReadFile(file, pixelData, header.dataSize);
    if (bytes_read != header.dataSize)
    {
        DEKI_LOG_ERROR("Failed to read texture pixel data");
        Deki::Memory::Free(pixelData);
        fs->CloseFile(file);
        return nullptr;
    }

    fs->CloseFile(file);

    PixelsToMemoryOrder(header, pixelData);
    Texture2D* texture = new Texture2D();
    if (!texture->LoadFromMemory(header, pixelData))
    {
        DEKI_LOG_ERROR("Failed to load texture from memory");
        Deki::Memory::Free(pixelData);
        delete texture;
        return nullptr;
    }

    // Pixel data is now owned by texture
    texture->data = pixelData;

    DEKI_LOG_INTERNAL("Loaded texture: %s (%dx%d, %s)", file_path, texture->width, texture->height,
                      GetFormatName(texture->format));

    return texture;
}
#endif  // !DEKI_EDITOR

bool Texture2D::LoadFromMemory(const Texture2D::Header& header, const uint8_t* pixelData)
{
    if (!pixelData)
    {
        return false;
    }

    // Set basic properties
    width = header.width;
    height = header.height;
    format = header.format;
    hasTransparency = (header.flags & DTEX_FLAG_HAS_TRANSPARENCY) != 0;
    hasAlpha = (header.flags & DTEX_FLAG_HAS_ALPHA) != 0;

    return true;
}

uint32_t Texture2D::GetBytesPerPixel(Texture2D::TextureFormat format)
{
    switch (format)
    {
        case Texture2D::TextureFormat::RGB888: return 3;
        case Texture2D::TextureFormat::RGBA8888: return 4;
        case Texture2D::TextureFormat::RGB565: return 2;
        case Texture2D::TextureFormat::RGB565A8: return 3;
        case Texture2D::TextureFormat::ALPHA8: return 1;
        default: return 0;
    }
}

const char* Texture2D::GetFormatName(Texture2D::TextureFormat format)
{
    switch (format)
    {
        case Texture2D::TextureFormat::RGB888: return "RGB888";
        case Texture2D::TextureFormat::RGBA8888: return "RGBA8888";
        case Texture2D::TextureFormat::RGB565: return "RGB565";
        case Texture2D::TextureFormat::RGB565A8: return "RGB565A8";
        case Texture2D::TextureFormat::ALPHA8: return "ALPHA8";
        default: return "Unknown";
    }
}

bool Texture2D::ValidateHeader(const Texture2D::Header& header)
{
    // Check magic number
    if (strncmp(header.magic, "2DTX", 4) != 0)
    {
        DEKI_LOG_ERROR("Invalid magic number in texture header");
        return false;
    }

    // Check version
    if (header.version != 1)
    {
        DEKI_LOG_ERROR("Unsupported texture version: %u", header.version);
        return false;
    }

    // Check dimensions
    if (header.width == 0 || header.height == 0)
    {
        DEKI_LOG_ERROR("Invalid texture dimensions: %ux%u", header.width, header.height);
        return false;
    }

    // Check format
    uint32_t bytesPerPixel = GetBytesPerPixel(header.format);
    if (bytesPerPixel == 0)
    {
        DEKI_LOG_ERROR("Unknown texture format: %u", static_cast<uint32_t>(header.format));
        return false;
    }

    // Validate data size in 64 bits: width * height * bpp can wrap in 32, and
    // a huge corrupt image would then match a small dataSize.
    const uint64_t expectedDataSize = uint64_t(header.width) * uint64_t(header.height) * bytesPerPixel;
    if (uint64_t(header.dataSize) != expectedDataSize)
    {
        DEKI_LOG_ERROR("Data size mismatch. Expected: %llu, Got: %u", (unsigned long long)expectedDataSize,
                       header.dataSize);
        return false;
    }

    return true;
}

}  // namespace Deki
