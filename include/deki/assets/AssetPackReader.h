#pragma once

#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <string>
#include <vector>

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

static constexpr char kDpackMagic[4] = { 'D', 'P', 'A', 'K' };
static constexpr uint32_t kDpackVersion = 1;

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

// A pack file loaded into memory.
struct LoadedPack
{
    uint8_t* data;  // the whole file
    size_t dataSize;
    uint32_t entryCount;

    struct AssetInfo
    {
        const uint8_t* ptr;  // into data
        uint32_t size;
        std::string typeName;
    };

    // GUID -> asset, pointing into data.
    std::unordered_map<std::string, AssetInfo> assets;
};

// One pack_index.bin entry: which pack file holds an asset.
#pragma pack(push, 1)
struct PackIndexEntry
{
    char guid[37];
    char packPath[48];  // Pack file name, in the index's own folder ("{guid}.pack")
    char padding[3];
};
#pragma pack(pop)
static_assert(sizeof(PackIndexEntry) == 88, "PackIndexEntry must be 88 bytes");

class DEKI_PACK_API AssetPackReader
{
public:
    static AssetPackReader& Instance();

    /// Reads pack_index.bin from `path`.
    bool LoadPackIndex(const char* path);

    bool IsInPack(const std::string& guid) const;

    /// The pack file holding `guid`, or "" when it is in no pack.
    std::string GetPackPath(const std::string& guid) const;

    /// Loads a pack file into memory and registers its assets. True when the
    /// pack is loaded, including when it already was.
    bool LoadPack(const char* packPath);

    /// The asset `guid` in a loaded pack, or nullptr when not found or its
    /// pack is not loaded.
    const LoadedPack::AssetInfo* GetAsset(const std::string& guid) const;

    /// Frees every loaded pack.
    void UnloadAll();

    bool HasPackIndex() const { return !m_GuidToPackPath.empty(); }

private:
    AssetPackReader() = default;
    ~AssetPackReader();

    // GUID -> pack file path.
    std::unordered_map<std::string, std::string> m_GuidToPackPath;

    // Pack path -> loaded pack.
    std::unordered_map<std::string, LoadedPack*> m_LoadedPacks;

    // GUID -> loaded pack, one lookup instead of two.
    std::unordered_map<std::string, LoadedPack*> m_GuidToLoadedPack;
};

}  // namespace Deki
