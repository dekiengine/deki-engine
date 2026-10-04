// AssetRef<T> is one component field that holds an asset's GUID (what scene
// files store) and, at runtime, the loaded asset, loading it on first use:
//
//   class SomeComponent {
//       AssetRef<SomeAsset> ref;
//   };
//
// Works with any asset type that has a static kAssetTypeName member.

#pragma once

#include <cstdint>
#include <string>
#include <deki/assets/AssetManager.h>

namespace Deki
{

/// The untyped part of AssetRef, so the asset system can reach guid and ptr
/// without knowing T.
struct AssetRefBase
{
    void* ptr = nullptr;
    std::string guid;            // compiled asset GUID, used to load
    std::string source;          // source asset GUID, for the editor's display; optional
    bool loadAttempted = false;  // stops a failed load being retried every frame
    uint64_t cachedEpoch = 0;    // AssetManager epoch when ptr was resolved; a mismatch means stale

    bool HasGuid() const { return !guid.empty(); }

    bool HasSource() const { return !source.empty(); }

    bool IsLoaded() const { return ptr != nullptr; }
};

/// Typed asset reference: use it like a T*. T needs a
/// `static constexpr const char* kAssetTypeName` member.
template <typename T>
struct AssetRef : AssetRefBase
{
    // Loads on first use when a GUID is set. Drops the cached pointer when the
    // AssetManager epoch changes, so it never returns a pointer freed by an
    // asset reload or hot reload.
    T* Get()
    {
        auto* mgr = Deki::AssetManager::Get();
        const uint64_t curEpoch = mgr ? mgr->GetEpoch() : 0;
        if (cachedEpoch != curEpoch)
        {
            ptr = nullptr;
            loadAttempted = false;
            cachedEpoch = curEpoch;
        }
        if (!ptr && !guid.empty() && !loadAttempted && mgr)
        {
            loadAttempted = true;  // set first, so a failure is not retried every frame
            ptr = mgr->LoadByGuidAndType(guid, T::kAssetTypeName);
            // Retry only when the asset system was not ready yet (at startup,
            // before the filesystem and GUID table are up). A real miss must
            // not retry, or it reopens the missing file every frame and floods
            // the log. If the asset is imported later, the epoch changes and
            // the check above allows a new attempt.
            if (!ptr && !mgr->IsReady())
            {
                loadAttempted = false;
            }
        }
        return static_cast<T*>(ptr);
    }

    const T* Get() const { return const_cast<AssetRef*>(this)->Get(); }

    T* operator->() { return Get(); }
    const T* operator->() const { return Get(); }
    T& operator*() { return *Get(); }
    const T& operator*() const { return *Get(); }

    operator T*() { return Get(); }
    operator const T*() const { return Get(); }

    // Loads the asset if needed, to tell whether it exists.
    operator bool() const { return const_cast<AssetRef*>(this)->Get() != nullptr; }

    // Used by the asset manager.
    AssetRef& operator=(T* p)
    {
        ptr = p;
        auto* mgr = Deki::AssetManager::Get();
        cachedEpoch = mgr ? mgr->GetEpoch() : 0;
        return *this;
    }

    bool operator==(const T* other) const { return Get() == other; }
    bool operator!=(const T* other) const { return Get() != other; }
    bool operator==(std::nullptr_t) const { return ptr == nullptr; }
    bool operator!=(std::nullptr_t) const { return ptr != nullptr; }
};

}  // namespace Deki
