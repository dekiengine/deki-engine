#include <deki/assets/AssetPackReader.h>
#include <deki/LogSystem.h>
#include <deki/Time.h>
#include <deki/providers/FileSystem.h>
#include <deki/providers/IFileSystem.h>
#include <deki/providers/Memory.h>
#include <cstring>

namespace Deki
{

namespace
{
/// Read a fixed-width char field from a pack file into a std::string.
///
/// These fields are `char[N]` in the on-disk struct and are only
/// NUL-terminated by convention. Passing one straight to std::string lets a
/// file with no terminator read past the field — into the next one, and off
/// the end of the table for the final entry. Bounded here instead.
std::string FixedFieldToString(const char* field, size_t capacity)
{
    size_t len = 0;
    while (len < capacity && field[len] != '\0')
        ++len;
    return std::string(field, len);
}
}  // namespace

AssetPackReader& AssetPackReader::Instance()
{
    static AssetPackReader instance;
    return instance;
}

AssetPackReader::~AssetPackReader()
{
    UnloadAll();
}

bool AssetPackReader::LoadPackIndex(const char* path)
{
    IFileSystem* fs = FileSystem::GetFileSystemForPath(path);
    if (!fs)
    {
        DEKI_LOG_ERROR("[PACK] No filesystem mounted for pack index '%s'", path);
        return false;
    }

    IFileSystem::FileHandle file = fs->OpenFile(path, IFileSystem::OpenMode::READ_BINARY);
    if (!file)
    {
        DEKI_LOG_ERROR("[PACK] Cannot open pack index '%s'", path);
        return false;
    }

    // Read entry count
    uint32_t entryCount = 0;
    if (fs->ReadFile(file, &entryCount, sizeof(uint32_t)) != sizeof(uint32_t))
    {
        DEKI_LOG_ERROR("[PACK] Pack index '%s' is too short to hold an entry count", path);
        fs->CloseFile(file);
        return false;
    }

    // Read entries
    for (uint32_t i = 0; i < entryCount; i++)
    {
        PackIndexEntry entry;
        if (fs->ReadFile(file, &entry, sizeof(PackIndexEntry)) != sizeof(PackIndexEntry))
        {
            // A short read used to `break` and then report success with the
            // full declared count, so a truncated index looked healthy.
            DEKI_LOG_ERROR("[PACK] Pack index '%s' is truncated: %u of %u entries readable",
                           path, (unsigned)i, (unsigned)entryCount);
            fs->CloseFile(file);
            m_GuidToPackPath.clear();
            return false;
        }

        // Same fixed-width fields as the pack entries: bound the read rather
        // than trusting a terminator to be present.
        std::string guid = FixedFieldToString(entry.guid, sizeof(entry.guid));
        if (guid.empty())
            continue;
        m_GuidToPackPath[guid] = FixedFieldToString(entry.packPath, sizeof(entry.packPath));
    }

    fs->CloseFile(file);
    DEKI_LOG_INTERNAL("[PACK] Loaded pack index: %u entries", entryCount);
    return true;
}

bool AssetPackReader::IsInPack(const std::string& guid) const
{
    return m_GuidToPackPath.find(guid) != m_GuidToPackPath.end();
}

std::string AssetPackReader::GetPackPath(const std::string& guid) const
{
    auto it = m_GuidToPackPath.find(guid);
    if (it != m_GuidToPackPath.end())
        return it->second;
    return "";
}

bool AssetPackReader::LoadPack(const char* packPath)
{
    // Already loaded?
    auto it = m_LoadedPacks.find(packPath);
    if (it != m_LoadedPacks.end())
        return true;

    uint32_t t0 = Time::GetTime();

    IFileSystem* fs = FileSystem::GetFileSystemForPath(packPath);
    if (!fs) return false;

    IFileSystem::FileHandle file = fs->OpenFile(packPath, IFileSystem::OpenMode::READ_BINARY);
    if (!file) return false;

    long fileSize = fs->GetFileSize(file);
    if (fileSize < static_cast<long>(sizeof(DPackHeader)))
    {
        fs->CloseFile(file);
        return false;
    }

    // Read entire pack into PSRAM
    uint8_t* data = static_cast<uint8_t*>(Memory::Allocate(fileSize, MemoryUse::External, "AssetPack"));
    if (!data)
    {
        fs->CloseFile(file);
        DEKI_LOG_ERROR("[PACK] Failed to allocate %ld bytes for pack", fileSize);
        return false;
    }

    size_t bytesRead = fs->ReadFile(file, data, fileSize);
    fs->CloseFile(file);

    if (bytesRead != static_cast<size_t>(fileSize))
    {
        Memory::Free(data);
        return false;
    }

    // Validate header
    const DPackHeader* header = reinterpret_cast<const DPackHeader*>(data);
    if (memcmp(header->magic, DPACK_MAGIC, 4) != 0 || header->version != DPACK_VERSION)
    {
        DEKI_LOG_ERROR("[PACK] Invalid pack file: %s", packPath);
        Memory::Free(data);
        return false;
    }

    // Everything below this point comes out of a file that may be corrupt (a
    // failing SD card is enough) and is used to form pointers, so each field is
    // checked against the buffer before it is trusted.
    const size_t packSize = static_cast<size_t>(fileSize);

    // The entry table must fit in the file. entryCount is a uint32 from the
    // file: compute in 64-bit so the multiply cannot wrap on the 32-bit
    // targets, where size_t is the same width as entryCount.
    const uint64_t tableBytes =
        static_cast<uint64_t>(header->entryCount) * sizeof(DPackEntry);
    if (tableBytes > static_cast<uint64_t>(packSize - sizeof(DPackHeader)))
    {
        DEKI_LOG_ERROR("[PACK] %s declares %u entries (%llu bytes) but is only %zu bytes",
                       packPath, header->entryCount,
                       static_cast<unsigned long long>(tableBytes), packSize);
        Memory::Free(data);
        return false;
    }

    // Parse entry table
    LoadedPack* pack = new LoadedPack();
    pack->data = data;
    pack->dataSize = fileSize;
    pack->entryCount = header->entryCount;

    const DPackEntry* entries = reinterpret_cast<const DPackEntry*>(data + sizeof(DPackHeader));

    for (uint32_t i = 0; i < header->entryCount; i++)
    {
        const DPackEntry& e = entries[i];

        // offset/size were previously used unchecked, so a bad entry pointed
        // assets anywhere and AssetManager then read `size` bytes from there.
        // 64-bit arithmetic keeps offset + size from wrapping past the check.
        const uint64_t end = static_cast<uint64_t>(e.offset) + static_cast<uint64_t>(e.size);
        if (end > static_cast<uint64_t>(packSize))
        {
            DEKI_LOG_ERROR("[PACK] %s entry %u spans %u..%llu, past the %zu-byte pack — skipping",
                           packPath, i, e.offset,
                           static_cast<unsigned long long>(end), packSize);
            continue;
        }

        LoadedPack::AssetInfo info;
        info.ptr = data + e.offset;
        info.size = e.size;
        // guid/typeName are fixed-width char arrays with no guarantee of a
        // terminator in the file; bound the length so std::string cannot run
        // off the end of the field.
        info.typeName = FixedFieldToString(e.typeName, sizeof(e.typeName));

        std::string guid = FixedFieldToString(e.guid, sizeof(e.guid));
        if (guid.empty())
        {
            DEKI_LOG_WARNING("[PACK] %s entry %u has an empty GUID — skipping", packPath, i);
            continue;
        }

        pack->assets[guid] = info;
        m_GuidToLoadedPack[guid] = pack;
    }

    m_LoadedPacks[packPath] = pack;

    uint32_t elapsed = Time::GetElapsed(t0);
    DEKI_LOG_INTERNAL("[PERF] Pack loaded: %s (%u assets, %ld bytes, %ums)",
                      packPath, header->entryCount, fileSize, elapsed);

    return true;
}

const LoadedPack::AssetInfo* AssetPackReader::GetAsset(const std::string& guid) const
{
    auto packIt = m_GuidToLoadedPack.find(guid);
    if (packIt == m_GuidToLoadedPack.end())
        return nullptr;

    auto assetIt = packIt->second->assets.find(guid);
    if (assetIt == packIt->second->assets.end())
        return nullptr;

    return &assetIt->second;
}

void AssetPackReader::UnloadAll()
{
    for (auto& [path, pack] : m_LoadedPacks)
    {
        if (pack->data)
            Memory::Free(pack->data);
        delete pack;
    }
    m_LoadedPacks.clear();
    m_GuidToLoadedPack.clear();
    m_GuidToPackPath.clear();
}

}  // namespace Deki
