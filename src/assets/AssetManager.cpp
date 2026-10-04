/**
 * @file AssetManager.cpp
 * @brief Unified asset manager implementation
 *
 * Type-erased: packages register their own loaders via REGISTER_ASSET_TYPE.
 * The core only knows about Scene (a core type).
 */

#include <deki/assets/AssetManager.h>
#include <deki/providers/FileSystem.h>
#include <deki/providers/IFileSystem.h>
#include <deki/assets/AssetLookupTable.h>
#include <deki/assets/AssetPackReader.h>
#include <deki/providers/Buffer.h>
#include <deki/Scene.h>

#include <deki/LogSystem.h>
#include <deki/Time.h>

#include <algorithm>
#include <cstdio>

#ifdef DEKI_EDITOR
#include <nlohmann/json.hpp>
#include <fstream>
#endif

namespace Deki
{

AssetManager* AssetManager::s_Instance = nullptr;

// Meyer's singleton to avoid static init order issues with REGISTER_ASSET_TYPE
struct LoaderEntry
{
    AssetLoadFn loader = nullptr;
    AssetUnloadFn unloader = nullptr;
    AssetMemLoadFn memLoader = nullptr;
    bool cacheable = true;
};

static std::unordered_map<std::string, LoaderEntry>& GetLoaderRegistry()
{
    static std::unordered_map<std::string, LoaderEntry> s_Reg;
    return s_Reg;
}

void AssetManager::RegisterLoader(const char* typeName, AssetLoadFn loader, AssetUnloadFn unloader,
                                  AssetMemLoadFn memLoader, bool cacheable)
{
    GetLoaderRegistry()[typeName] = { loader, unloader, memLoader, cacheable };
}

bool AssetManager::HasLoader(const char* typeName)
{
    if (!typeName)
    {
        return false;
    }
    return GetLoaderRegistry().count(typeName) > 0;
}

AssetManager* AssetManager::Get()
{
    if (!s_Instance)
    {
        s_Instance = new AssetManager();
        // Register Scene loader (core type) - NOT cacheable: each load returns
        // a fresh instance because scenes are mutable scene graphs, not shared data
        RegisterLoader(
            "Scene", [](const char* p) -> void* { return Scene::LoadInternal(p); },
            [](void* a) { delete static_cast<Scene*>(a); },
            [](const uint8_t* d, size_t s) -> void* { return Scene::LoadFromMemory(d, s); }, false);
    }
    return s_Instance;
}

void AssetManager::Shutdown()
{
    if (s_Instance)
    {
        s_Instance->UnloadAll();
        AssetLookupTable::Unload();
        delete s_Instance;
        s_Instance = nullptr;
    }
}

AssetManager::~AssetManager()
{
    UnloadAll();
}

void AssetManager::UnloadAll()
{
    DEKI_LOG_INTERNAL("AssetManager: Unloading all assets");

    auto& registry = GetLoaderRegistry();

    for (auto& [typeName, assets] : m_LoadedAssets)
    {
        auto regIt = registry.find(typeName);
        AssetUnloadFn unloader = (regIt != registry.end()) ? regIt->second.unloader : nullptr;

        for (void* asset : assets)
        {
            if (unloader)
            {
                unloader(asset);
            }
        }
    }
    m_LoadedAssets.clear();
    m_TypeCache.clear();
    m_ByteCache.clear();

    ++m_Epoch;
}

void AssetManager::InvalidateAsset(const char* typeName, const std::string& guid)
{
    std::string path = LookupPath(guid);
    if (path.empty())
    {
        return;
    }

    std::string type(typeName);
    auto cacheIt = m_TypeCache.find(type);
    if (cacheIt == m_TypeCache.end())
    {
        return;
    }

    auto it = cacheIt->second.find(path);
    if (it == cacheIt->second.end())
    {
        return;
    }

    void* asset = it->second;
    cacheIt->second.erase(it);

    // Remove from ownership vector
    auto& assets = m_LoadedAssets[type];
    auto vecIt = std::find(assets.begin(), assets.end(), asset);
    if (vecIt != assets.end())
    {
        assets.erase(vecIt);
    }

    // Free via registered unloader
    auto& registry = GetLoaderRegistry();
    auto regIt = registry.find(type);
    if (regIt != registry.end() && regIt->second.unloader)
    {
        regIt->second.unloader(asset);
    }

    ++m_Epoch;

    DEKI_LOG_INTERNAL("AssetManager: Invalidated %s cache for GUID: %s", typeName, guid.c_str());
}

void AssetManager::ClearCache(const char* typeName)
{
    std::string type(typeName);

    auto& registry = GetLoaderRegistry();
    auto regIt = registry.find(type);
    AssetUnloadFn unloader = (regIt != registry.end()) ? regIt->second.unloader : nullptr;

    auto assetsIt = m_LoadedAssets.find(type);
    if (assetsIt != m_LoadedAssets.end())
    {
        for (void* asset : assetsIt->second)
        {
            if (unloader)
            {
                unloader(asset);
            }
        }
        assetsIt->second.clear();
    }

    m_TypeCache.erase(type);

    ++m_Epoch;

    DEKI_LOG_INTERNAL("AssetManager: Cleared all %s caches", typeName);
}

void AssetManager::DropCache()
{
    DEKI_LOG_INTERNAL("AssetManager: Dropping lookup cache (assets not freed)");
    m_TypeCache.clear();
    m_ByteCache.clear();
}

void AssetManager::RegisterGuid(const std::string& guid, const std::string& path)
{
    m_GuidToPath[guid] = path;
}

void AssetManager::RemoveGuid(const std::string& guid)
{
    if (m_GuidToPath.erase(guid) > 0)
    {
        ++m_Epoch;  // refs holding this GUID must re-resolve (to null)
    }
}

bool AssetManager::ReadWholeFile(const std::string& path, std::vector<uint8_t>& out)
{
    IFileSystem* fs = FileSystem::GetFileSystemForPath(path.c_str());
    if (!fs)
    {
        return false;
    }
    IFileSystem::FileHandle file = fs->OpenFile(path.c_str(), IFileSystem::OpenMode::ReadBinary);
    if (!file)
    {
        return false;
    }
    const long size = fs->GetFileSize(file);
    bool ok = false;
    if (size > 0)
    {
        out.resize(static_cast<size_t>(size));
        ok = fs->ReadFile(file, out.data(), out.size()) == out.size();
    }
    fs->CloseFile(file);
    if (!ok)
    {
        out.clear();
    }
    return ok;
}

bool AssetManager::ReadWholeFile(const std::string& path, Buffer<uint8_t>& out, Memory::Region region)
{
    IFileSystem* fs = FileSystem::GetFileSystemForPath(path.c_str());
    if (!fs)
    {
        return false;
    }
    IFileSystem::FileHandle file = fs->OpenFile(path.c_str(), IFileSystem::OpenMode::ReadBinary);
    if (!file)
    {
        return false;
    }
    const long size = fs->GetFileSize(file);
    bool ok = false;
    if (size > 0)
    {
        if (!out.Allocate(static_cast<size_t>(size), region))
        {
            DEKI_LOG_ERROR("AssetManager: no room to read %s (%ld bytes)", path.c_str(), size);
        }
        else
        {
            ok = fs->ReadFile(file, out.Data(), out.Count()) == out.Count();
        }
    }
    fs->CloseFile(file);
    if (!ok)
    {
        out.Reset();
    }
    return ok;
}

const std::string& AssetManager::LookupPath(const std::string& guid) const
{
    static const std::string kEmpty;
    // Reuse m_LookupBuffer to avoid per-call heap allocations
    auto buildPath = [this](const std::string& suffix) -> const std::string&
    {
        m_LookupBuffer.clear();
        if (m_CacheDirectory.empty())
        {
            m_LookupBuffer = suffix;
        }
        else if (m_CacheDirectory.back() == '/')
        {
            m_LookupBuffer = m_CacheDirectory;
            m_LookupBuffer += suffix;
        }
        else
        {
            m_LookupBuffer = m_CacheDirectory;
            m_LookupBuffer += '/';
            m_LookupBuffer += suffix;
        }
        return m_LookupBuffer;
    };

    // Check explicit GUID-to-path map first (always available in editor).
    // This has correct paths for all registered assets, including those
    // whose cache files aren't stored as flat {guid} files.
    auto it = m_GuidToPath.find(guid);
    if (it != m_GuidToPath.end())
    {
        return buildPath(it->second);
    }

    // On embedded, all assets are stored as {cacheDir}/{guid} on the SD card.
    // The filesystem handles "not found" - no need to validate here.
    if (AssetLookupTable::IsLoaded())
    {
        return buildPath(guid);
    }

    return kEmpty;
}

std::string AssetManager::LookupGuidByKey(const std::string& key) const
{
    DEKI_LOG_INTERNAL("AssetManager::LookupGuidByKey: key='%s'", key.c_str());

    // Try JSON-based table first (editor mode)
    auto it = m_GuidToPath.find(key);
    if (it != m_GuidToPath.end())
    {
        DEKI_LOG_INTERNAL("AssetManager::LookupGuidByKey: Found in JSON table: %s", it->second.c_str());
        return it->second;
    }

    // Fallback to binary lookup table (embedded runtime)
    if (AssetLookupTable::IsLoaded())
    {
        uint32_t hash = AssetLookupTable::HashString(key.c_str());
        const char* foundGuid = AssetLookupTable::Lookup(hash);
        DEKI_LOG_INTERNAL("AssetManager::LookupGuidByKey: Binary lookup hash=0x%08X, found=%s", hash,
                          foundGuid ? foundGuid : "(null)");
        if (foundGuid)
        {
            return foundGuid;
        }
    }
    else
    {
        DEKI_LOG_INTERNAL("AssetManager::LookupGuidByKey: Binary table not loaded");
    }

    DEKI_LOG_WARNING("AssetManager::LookupGuidByKey: Key not found: '%s'", key.c_str());
    return "";
}

bool AssetManager::LoadAssetLookupTable(const uint8_t* data, size_t size)
{
    return AssetLookupTable::Load(data, size);
}

bool AssetManager::LoadAssetRoot(const char* root)
{
    const std::string tablePath = std::string(root) + "asset_table.bin";
    IFileSystem* fs = FileSystem::GetFileSystemForPath(tablePath.c_str());
    if (!fs)
    {
        DEKI_LOG_ERROR("AssetManager: no filesystem for %s", tablePath.c_str());
        return false;
    }
    IFileSystem::FileHandle handle = fs->OpenFile(tablePath.c_str(), IFileSystem::OpenMode::ReadBinary);
    if (!handle)
    {
        DEKI_LOG_ERROR("AssetManager: %s is missing; the build's assets are not there", tablePath.c_str());
        return false;
    }
    const long size = fs->GetFileSize(handle);
    if (size <= 0)
    {
        fs->CloseFile(handle);
        DEKI_LOG_ERROR("AssetManager: %s is empty", tablePath.c_str());
        return false;
    }

    // The lookup table points into this block, so it lives as long as the
    // table does. External: it is read on a lookup by key, not per frame.
    static Buffer<uint8_t> s_Table;
    if (!s_Table.Allocate(static_cast<size_t>(size), Memory::External))
    {
        fs->CloseFile(handle);
        DEKI_LOG_ERROR("AssetManager: no room for %s (%ld bytes)", tablePath.c_str(), size);
        return false;
    }
    const size_t read = fs->ReadFile(handle, s_Table.Data(), static_cast<size_t>(size));
    fs->CloseFile(handle);
    if (read != static_cast<size_t>(size) || !AssetLookupTable::Load(s_Table.Data(), read))
    {
        DEKI_LOG_ERROR("AssetManager: could not load %s", tablePath.c_str());
        return false;
    }

    SetCacheDirectory(root);
    AssetPackReader::Instance().LoadPackIndex((std::string(root) + "pack_index.bin").c_str());
    DEKI_LOG_INFO("Assets: %u in %s", AssetLookupTable::GetEntryCount(), root);
    return true;
}

bool AssetManager::LoadGuidTable(const std::string& path)
{
#ifdef DEKI_EDITOR
    std::ifstream file(path);
    if (!file.is_open())
    {
        DEKI_LOG_ERROR("AssetManager: Failed to open GUID table: %s", path.c_str());
        return false;
    }

    try
    {
        nlohmann::json j;
        file >> j;

        // Clear existing mappings
        m_GuidToPath.clear();

        // Load all GUID -> path mappings
        for (auto it = j.begin(); it != j.end(); ++it)
        {
            m_GuidToPath[it.key()] = it.value().get<std::string>();
        }

        DEKI_LOG_INTERNAL("AssetManager: Loaded %zu GUIDs from %s", m_GuidToPath.size(), path.c_str());
        return true;
    }
    catch (const std::exception& e)
    {
        DEKI_LOG_ERROR("AssetManager: Failed to parse GUID table: %s", e.what());
        return false;
    }
#else
    // Runtime: GUID table is embedded in binary assets, not loaded from JSON
    (void)path;
    DEKI_LOG_WARNING("AssetManager: LoadGuidTable not available at runtime");
    return false;
#endif
}

void* AssetManager::LoadByGuidAndType(const std::string& guid, const char* assetType, bool cache)
{
    // Skip loading when asset system isn't ready (e.g., during scene compilation
    // inside ImportAllAssets, before filesystem and cache directory are set up)
    if (!IsReady())
    {
        return nullptr;
    }

    std::string path = LookupPath(guid);
    if (path.empty())
    {
        // A reference to an asset that no longer exists (deleted, or never
        // imported) is a normal state, like a missing reference in Unity: it
        // resolves to null and renders nothing. Keep it off the console - a
        // ref re-resolves every frame, so a warning here would flood the log.
        // Opt back in via DEKI_LOG_INTERNAL_ENABLED when debugging.
        DEKI_LOG_INTERNAL("AssetManager: GUID not found in table: %s", guid.c_str());
        return nullptr;
    }

    std::string type(assetType);
    auto& registry = GetLoaderRegistry();
    auto regIt = registry.find(type);
    if (regIt == registry.end())
    {
        DEKI_LOG_WARNING("AssetManager: No loader registered for asset type: %s", assetType);
        return nullptr;
    }

    bool typeCacheable = regIt->second.cacheable;

    // Pointer cache: for naturally cacheable types (those registered with cacheable=true).
    // Returns the same instance every time - no re-parsing, no re-allocation.
    if (typeCacheable)
    {
        auto& ptrCache = m_TypeCache[type];
        auto cacheIt = ptrCache.find(path);
        if (cacheIt != ptrCache.end())
        {
            DEKI_LOG_INTERNAL("AssetManager: Using cached %s: %s", assetType, path.c_str());
            return cacheIt->second;
        }
    }

    // Byte cache: for non-cacheable types when cache=true is requested.
    // Pins the raw file bytes in RAM so subsequent loads skip disk I/O,
    // but always returns a fresh instance by deserializing from the cached bytes.
    if (!typeCacheable && cache && regIt->second.memLoader)
    {
        auto byteCacheIt = m_ByteCache.find(path);
        if (byteCacheIt != m_ByteCache.end())
        {
            // Byte cache hit: create fresh instance from pinned bytes
            DEKI_LOG_INTERNAL("AssetManager: Loading fresh %s from byte cache: %s", assetType, path.c_str());
            void* asset = regIt->second.memLoader(byteCacheIt->second.Data(), byteCacheIt->second.Count());
            if (asset)
            {
                m_LoadedAssets[type].push_back(asset);
                return asset;
            }
        }
        else
        {
            // Byte cache miss: read file bytes, pin in RAM, create from bytes.
            // Through the filesystem provider, like every other engine read:
            // a raw fopen cannot resolve a mounted prefix such as "S:/" on a
            // device, so the byte cache silently never worked there.
            Buffer<uint8_t> bytes;
            if (ReadWholeFile(path, bytes, Memory::External) && bytes.Count() > 0)
            {
                m_ByteCache[path] = std::move(bytes);
                auto& cached = m_ByteCache[path];
                DEKI_LOG_INTERNAL("AssetManager: Pinned %s bytes in RAM (%zu bytes): %s", assetType, cached.Count(),
                                  path.c_str());
                void* asset = regIt->second.memLoader(cached.Data(), cached.Count());
                if (asset)
                {
                    m_LoadedAssets[type].push_back(asset);
                    return asset;
                }
            }
            // Fall through to regular loader if byte cache failed
        }
    }

    // Check if asset is in a pack file
    auto& packReader = AssetPackReader::Instance();
    if (packReader.HasPackIndex() && packReader.IsInPack(guid))
    {
        // Load the pack (loads all assets in it) if not already loaded
        std::string packPath = packReader.GetPackPath(guid);
        if (!packPath.empty())
        {
            packReader.LoadPack(packPath.c_str());

            // After loading the pack, load all pack assets into our cache
            const auto* packAsset = packReader.GetAsset(guid);
            if (packAsset && packAsset->ptr && packAsset->size > 0)
            {
                void* asset = LoadFromMemory(guid, assetType, packAsset->ptr, packAsset->size);
                if (asset)
                {
                    return asset;
                }
            }
        }
    }

    // A missing file resolves to null silently. A deleted (or not-yet-present)
    // asset is a normal "missing reference" - Unity-style - not an error: the
    // ref renders nothing and reconnects if the asset comes back. Probe here so
    // the per-type loaders never run for an absent file and each spam their own
    // "failed to open" error every frame. Valid assets hit the caches above and
    // never reach this probe; a file that exists but fails to parse still falls
    // through to the loader and logs a real error below.
    if (IFileSystem* fs = FileSystem::GetFileSystemForPath(path.c_str()))
    {
        if (!fs->FileExists(path.c_str()))
        {
            return nullptr;
        }
    }

    // Load via registered loader (from filesystem)
    void* asset = regIt->second.loader(path.c_str());
    if (asset)
    {
        if (typeCacheable)
        {
            m_TypeCache[type][path] = asset;
        }
        // Always track in m_LoadedAssets for cleanup via UnloadAll().
        // Callers that take ownership should call ReleaseAssetTracking() to prevent double-free.
        m_LoadedAssets[type].push_back(asset);
    }
    else
    {
        DEKI_LOG_WARNING("AssetManager: Failed to load %s: %s", assetType, path.c_str());
    }

    return asset;
}

void* AssetManager::LoadFromMemory(const std::string& guid, const char* assetType, const uint8_t* data, size_t size)
{
    if (!data || size == 0 || !assetType)
    {
        return nullptr;
    }

    std::string type(assetType);
    auto& registry = GetLoaderRegistry();
    auto it = registry.find(type);
    if (it == registry.end() || !it->second.memLoader)
    {
        return nullptr;
    }

    bool cacheable = it->second.cacheable;

    void* asset = it->second.memLoader(data, size);
    if (asset)
    {
        if (cacheable)
        {
            std::string path = LookupPath(guid);
            if (!path.empty())
            {
                m_TypeCache[type][path] = asset;
            }
        }
        m_LoadedAssets[type].push_back(asset);
    }

    return asset;
}

bool AssetManager::IsTypeCacheable(const char* typeName) const
{
    auto& registry = GetLoaderRegistry();
    auto it = registry.find(std::string(typeName));
    if (it == registry.end())
    {
        return true;  // Unknown types default to cacheable
    }
    return it->second.cacheable;
}

void AssetManager::ReleaseAssetTracking(const char* typeName, void* asset)
{
    if (!asset)
    {
        return;
    }

    std::string type(typeName);
    auto it = m_LoadedAssets.find(type);
    if (it == m_LoadedAssets.end())
    {
        return;
    }

    auto& vec = it->second;
    auto vecIt = std::find(vec.begin(), vec.end(), asset);
    if (vecIt != vec.end())
    {
        vec.erase(vecIt);
        DEKI_LOG_INTERNAL("AssetManager: Released tracking of %s %p (caller owns)", typeName, asset);
    }
}

}  // namespace Deki
