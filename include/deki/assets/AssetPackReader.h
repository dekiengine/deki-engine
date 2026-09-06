#pragma once

#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <string>
#include <vector>

// DLL export/import
#ifdef DEKI_EDITOR
#ifdef _WIN32
#ifdef DEKI_ENGINE_EXPORTS
#define DEKI_PACK_API __declspec(dllexport)
#else
#define DEKI_PACK_API __declspec(dllimport)
#endif
#else
#define DEKI_PACK_API
#endif
#else
#define DEKI_PACK_API
#endif

namespace Deki
{

// Pack file format constants
static constexpr char DPACK_MAGIC[4] = { 'D', 'P', 'A', 'K' };
static constexpr uint32_t DPACK_VERSION = 1;

#pragma pack(push, 1)
struct DPackHeader
{
    char magic[4];
    uint32_t version;
    uint32_t entryCount;
    uint32_t reserved;
};

struct DPackEntry
{
    char guid[37];
    char typeName[20];
    uint32_t offset;
    uint32_t size;
    char padding[3];
};
#pragma pack(pop)

static_assert(sizeof(DPackHeader) == 16, "DPackHeader must be 16 bytes");
static_assert(sizeof(DPackEntry) == 68, "DPackEntry must be 68 bytes");

// Represents a loaded pack file in memory
struct LoadedPack
{
    uint8_t* data;  // Raw pack data (entire file in PSRAM)
    size_t dataSize;
    uint32_t entryCount;

    struct AssetInfo
    {
        const uint8_t* ptr;  // Pointer into data buffer
        uint32_t size;
        std::string typeName;
    };

    // GUID → asset info (points into data buffer)
    std::unordered_map<std::string, AssetInfo> assets;
};

// Pack index entry: maps asset GUID → pack file path
#pragma pack(push, 1)
struct PackIndexEntry
{
    char guid[37];      // Asset GUID
    char packPath[48];  // Pack file path on SD card (e.g., "S:/{guid}.pack")
    char padding[3];
};
#pragma pack(pop)
static_assert(sizeof(PackIndexEntry) == 88, "PackIndexEntry must be 88 bytes");

class DEKI_PACK_API AssetPackReader
{
public:
    static AssetPackReader& Instance();

    // Load pack index (pack_index.bin) from filesystem
    bool LoadPackIndex(const char* path);

    // Check if a GUID is in any pack
    bool IsInPack(const std::string& guid) const;

    // Get the pack file path for a GUID (empty if not in any pack)
    std::string GetPackPath(const std::string& guid) const;

    // Load a pack file into memory and register all assets
    // Returns true if pack was loaded (or already loaded)
    bool LoadPack(const char* packPath);

    // Get asset data from a loaded pack
    // Returns nullptr if not found or pack not loaded
    const LoadedPack::AssetInfo* GetAsset(const std::string& guid) const;

    // Unload all packs and free memory
    void UnloadAll();

    // Check if any packs are available
    bool HasPackIndex() const { return !m_GuidToPackPath.empty(); }

private:
    AssetPackReader() = default;
    ~AssetPackReader();

    // GUID → pack file path
    std::unordered_map<std::string, std::string> m_GuidToPackPath;

    // Pack path → loaded pack data
    std::unordered_map<std::string, LoadedPack*> m_LoadedPacks;

    // GUID → loaded pack (for fast lookup without going through m_GuidToPackPath first)
    std::unordered_map<std::string, LoadedPack*> m_GuidToLoadedPack;
};

}  // namespace Deki
