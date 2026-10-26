// Loads assets by path or GUID, on every platform:
// - Load<T>() takes a path and finds the GUID in the lookup table
// - LoadByGuidAndType() takes a GUID and a type name
//
// The lookup table is read at startup from a file the build exports.

#pragma once

#include <string>
#include <deki/providers/Buffer.h>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <deki/assets/AssetLookupTable.h>

// Loads an asset from a filesystem path; nullptr on failure.
using AssetLoadFn = void* (*)(const char* path);

// Loads an asset from the file's bytes; nullptr on failure.
using AssetMemLoadFn = void* (*)(const uint8_t* data, size_t size);

// DLL export/import for the AssetManager singleton.
#ifdef DEKI_EDITOR
#ifdef _WIN32
#ifdef DEKI_ENGINE_EXPORTS
#define DEKI_ASSET_MANAGER_API __declspec(dllexport)
#else
#define DEKI_ASSET_MANAGER_API __declspec(dllimport)
#endif
#else
#define DEKI_ASSET_MANAGER_API
#endif
#else
#define DEKI_ASSET_MANAGER_API
#endif

// Frees a loaded asset.
using AssetUnloadFn = void (*)(void* asset);

class Scene;

namespace Deki
{

class DEKI_ASSET_MANAGER_API AssetManager
{
public:
    static AssetManager* Get();

    /// Frees all assets and the singleton.
    static void Shutdown();

    /// Loads the asset of type T at `path`, written without the "assets/"
    /// prefix and extension (such as "scenes/Demo"). Returns nullptr on
    /// failure. T is any type with a registered loader (Scene is built in;
    /// packages register theirs with RegisterLoader). The type is part of the
    /// lookup key, so assets of different types can share a path.
    ///
    /// Cacheable types are loaded once and shared, and `cache` does nothing
    /// for them. Non-cacheable types (such as Scene) return a fresh instance on
    /// every call; with `cache`, the file's bytes stay in RAM so later loads
    /// skip the disk.
    ///
    ///   auto* scene = AssetManager::Get()->Load<Scene>("scenes/Demo");        // fresh from disk
    ///   auto* scene = AssetManager::Get()->Load<Scene>("scenes/Demo", true);  // fresh from cached bytes
    template <typename T>
    T* Load(const char* path, bool cache = false);

    /// Registers how to load assets of type `typeName` (must match
    /// T::kAssetTypeName). `unloader` frees one (delete when null);
    /// `memLoader`, optional, loads from bytes in memory (pack files). A
    /// `cacheable` type is loaded once and shared; otherwise each load returns
    /// a fresh instance that the caller owns.
    static void RegisterLoader(const char* typeName, AssetLoadFn loader, AssetUnloadFn unloader = nullptr,
                               AssetMemLoadFn memLoader = nullptr, bool cacheable = true);

    /// True when a loader is registered for `typeName`. The editor treats any
    /// .asset whose "type" has a loader (and no texture compiler) as a generic
    /// data asset, like a ScriptableObject: its JSON compiles straight to a
    /// MessagePack cache with no per-type editor.
    static bool HasLoader(const char* typeName);

    /// Loads the asset `guid` of type `assetType` (as registered with
    /// RegisterLoader), or returns nullptr. `cache` works as in Load<T>().
    void* LoadByGuidAndType(const std::string& guid, const char* assetType, bool cache = false);

    /// Loads an asset from its file's bytes (pack files). `guid` is the cache
    /// key. Returns nullptr on failure.
    void* LoadFromMemory(const std::string& guid, const char* assetType, const uint8_t* data, size_t size);

    /// Frees every loaded asset. Every pointer returned before is invalid
    /// afterwards.
    void UnloadAll();

    /// Frees the cached asset `guid` of type `typeName` (such as "Sprite"), so
    /// the next load reads it from disk again.
    void InvalidateAsset(const char* typeName, const std::string& guid);

    /// Frees every cached asset of type `typeName` (such as "BitmapFont"), so
    /// the next loads read from disk again.
    void ClearCache(const char* typeName);

    /// Frees every cached asset whose GUID is not in `usedGuids`, and moves
    /// the epoch, so an AssetRef that pointed at one loads it again when next
    /// asked. Returns how many were freed. Engine::ReleaseUnusedAssets works
    /// out the list from the live objects.
    size_t UnloadUnused(const std::vector<std::string>& usedGuids);

    /// Forgets the cache without freeing the assets, which may still be in
    /// use; the next load reads a fresh copy from disk. The editor calls it
    /// when play mode stops. Leaves the epoch alone, since existing
    /// AssetRef::ptr values still point at live memory.
    void DropCache();

    /// Goes up whenever asset memory is freed or replaced. AssetRef compares
    /// its cachedEpoch with it on every Get() to notice stale pointers after
    /// hot reloads, re-imports and the like.
    uint64_t GetEpoch() const noexcept { return m_Epoch; }

    /// Makes every AssetRef drop its cached pointer and resolve again on its
    /// next Get(). Call it when the set of loadable assets changes without
    /// memory being freed, such as an asset imported live or restored by undo,
    /// so refs that resolved to null can find it.
    void AdvanceEpoch() noexcept { ++m_Epoch; }

    /// False only for types registered as non-cacheable.
    bool IsTypeCacheable(const char* typeName) const;

    /// Stops tracking a non-cacheable asset whose ownership another system
    /// (such as SceneSystem) has taken, so UnloadAll() does not free it twice.
    void ReleaseAssetTracking(const char* typeName, void* asset);

    /// Loads the editor's GUID-to-path table from a JSON file. Returns false
    /// on failure.
    bool LoadGuidTable(const std::string& path);

    /// Uses the runtime's binary lookup table in `data`, which must stay valid
    /// while lookups run. Returns false on failure.
    bool LoadAssetLookupTable(const uint8_t* data, size_t size);

    /// Uses a build's exported assets under `root` (Storage::AssetRoot): reads
    /// root/asset_table.bin and root/pack_index.bin and loads every asset from
    /// root. The engine calls it at startup for internal storage; the package
    /// that mounts external storage calls it once the card is up. Returns
    /// false, with the reason logged, when the table cannot be read.
    bool LoadAssetRoot(const char* root);

    /// Maps `guid` to the filesystem path `path` (editor).
    void RegisterGuid(const std::string& guid, const std::string& path);

    /// Forgets `guid` when its asset is deleted (editor). An AssetRef still
    /// holding it then loads null and draws nothing, instead of reopening a
    /// missing file and flooding the log. Bumps the epoch so those refs
    /// resolve again.
    void RemoveGuid(const std::string& guid);

    /// Directory asset files are read from: the project's cache directory in
    /// the editor, the storage root (such as "S:/") on a device.
    void SetCacheDirectory(const std::string& dir) { m_CacheDirectory = dir; }

    const std::string& GetCacheDirectory() const { return m_CacheDirectory; }

    /// True once assets can be loaded: the cache directory is set (editor edit
    /// mode) or the binary lookup table is loaded (play mode, device).
    /// AssetRef::Get() skips loading until then, such as while ImportAllAssets
    /// compiles scenes before the filesystem is up.
    bool IsReady() const { return !m_CacheDirectory.empty() || AssetLookupTable::IsLoaded(); }

    /// Full path (cache directory + entry) for a GUID, or "" when unknown. The
    /// reference points into a reused buffer and is valid until the next
    /// LookupPath call; it avoids a string copy on every AssetRef resolution.
    const std::string& LookupPath(const std::string& guid) const;

    /// Reads a whole file through the filesystem provider. False when the file
    /// cannot be opened or read in full (out is then empty).
    static bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out);

    /// Reads a whole file into a Buffer in `region`, through the engine's
    /// filesystem. Use it for anything big (a mesh, a texture): a std::vector
    /// lands on the internal heap and aborts the board when it does not fit,
    /// while this logs the size and returns false.
    static bool ReadWholeFile(const std::string& path, Buffer<uint8_t>& out, Memory::Region region);

private:
    AssetManager() = default;
    ~AssetManager();

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    /// The GUID for a "path:type" key (such as "scenes/Demo:Scene"), or "".
    /// Not a template, so it can cross the DLL boundary.
    std::string LookupGuidByKey(const std::string& key) const;

    // GUID -> file name; prepend m_CacheDirectory for the full path.
    std::unordered_map<std::string, std::string> m_GuidToPath;

    std::string m_CacheDirectory;

    // Reused by LookupPath so it does not allocate per call.
    mutable std::string m_LookupBuffer;

    // typeName -> (path -> asset), for cacheable types: the same pointer is
    // returned every time.
    std::unordered_map<std::string, std::unordered_map<std::string, void*>> m_TypeCache;

    // path -> file bytes, for non-cacheable types (Scene) loaded with
    // cache=true; each load builds a fresh instance from them. Held in
    // External memory as Buffers, so a file too big for a low-RAM board fails
    // the load instead of aborting.
    std::unordered_map<std::string, Buffer<uint8_t>> m_ByteCache;

    // typeName -> assets to free on cleanup.
    std::unordered_map<std::string, std::vector<void*>> m_LoadedAssets;

    // Goes up on every free or replace of asset memory (UnloadAll,
    // InvalidateAsset, ClearCache); AssetRef watches it to drop dangling
    // pointers. Starts at 1 so a new AssetRef (cachedEpoch 0) always misses on
    // its first Get.
    uint64_t m_Epoch = 1;

    static AssetManager* s_Instance;
};

template <typename T>
T* AssetManager::Load(const char* path, bool cache)
{
    if (!path)
    {
        return nullptr;
    }

    // Backslashes to forward slashes, extension removed.
    std::string normalized(path);
    for (char& c : normalized)
    {
        if (c == '\\')
        {
            c = '/';
        }
    }
    auto dot = normalized.rfind('.');
    auto slash = normalized.rfind('/');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
    {
        normalized = normalized.substr(0, dot);
    }

    std::string key = normalized + ":" + T::kAssetTypeName;

    // Checks both the editor's JSON table and the binary lookup table.
    std::string guid = LookupGuidByKey(key);
    if (guid.empty())
    {
        return nullptr;
    }

    return static_cast<T*>(LoadByGuidAndType(guid, T::kAssetTypeName, cache));
}

}  // namespace Deki

// Registers an asset type from a static initializer:
//   REGISTER_ASSET_TYPE(MyAssetClass, MyAssetClass::Load)
// MyAssetClass needs kAssetTypeName and a static Load(const char*).
#define REGISTER_ASSET_TYPE(TypeName, LoadFunc)                                                                        \
    namespace                                                                                                          \
    {                                                                                                                  \
    struct AssetTypeReg##TypeName                                                                                      \
    {                                                                                                                  \
        AssetTypeReg##TypeName()                                                                                       \
        {                                                                                                              \
            Deki::AssetManager::RegisterLoader(                                                                        \
                TypeName::kAssetTypeName, [](const char* p) -> void* { return LoadFunc(p); },                          \
                [](void* a) { delete static_cast<TypeName*>(a); });                                                    \
        }                                                                                                              \
    };                                                                                                                 \
    static AssetTypeReg##TypeName s_AssetReg##TypeName;                                                                \
    }

// The same, plus a loader from memory for pack files:
//   TypeName* MemLoadFunc(const uint8_t* data, size_t size)
#define REGISTER_ASSET_TYPE_WITH_MEMLOAD(TypeName, LoadFunc, MemLoadFunc)                                              \
    namespace                                                                                                          \
    {                                                                                                                  \
    struct AssetTypeReg##TypeName                                                                                      \
    {                                                                                                                  \
        AssetTypeReg##TypeName()                                                                                       \
        {                                                                                                              \
            Deki::AssetManager::RegisterLoader(                                                                        \
                TypeName::kAssetTypeName, [](const char* p) -> void* { return LoadFunc(p); },                          \
                [](void* a) { delete static_cast<TypeName*>(a); },                                                     \
                [](const uint8_t* d, size_t s) -> void* { return MemLoadFunc(d, s); });                                \
        }                                                                                                              \
    };                                                                                                                 \
    static AssetTypeReg##TypeName s_AssetReg##TypeName;                                                                \
    }
