/**
 * @file AssetRef.h
 * @brief Asset reference wrapper for automatic GUID storage and asset loading
 *
 * AssetRef<T> provides a single field that handles both:
 * - Runtime: pointer to loaded asset + GUID for loading
 * - Editor: GUID string for serialization
 *
 * Usage:
 *   class SomeComponent {
 *       AssetRef<SomeAsset> ref;  // Single field handles everything
 *   };
 *
 * Works with any asset type that has a static kAssetTypeName member.
 * No per-type registration needed in this file.
 */

#pragma once

#include <cstdint>
#include <string>
#include <deki/assets/AssetManager.h>

namespace Deki
{

/**
 * @brief Base class for type-erased access to AssetRef
 *
 * This allows the asset loading system to access the guid and ptr
 * without knowing the template type T.
 */
struct AssetRefBase
{
    void* ptr = nullptr;
    std::string guid;            // Baked/compiled asset GUID (for runtime lookup)
    std::string source;          // Source asset GUID (for editor display, optional)
    bool loadAttempted = false;  // Track if load was attempted (prevents retry spam)
    uint64_t cachedEpoch = 0;    // AssetManager epoch when ptr was resolved; mismatch -> stale

    // Check if GUID is set
    bool HasGuid() const { return !guid.empty(); }

    // Check if source GUID is set
    bool HasSource() const { return !source.empty(); }

    // Check if asset is loaded
    bool IsLoaded() const { return ptr != nullptr; }
};

/**
 * @brief Type-safe asset reference wrapper
 *
 * Provides transparent access to the underlying asset pointer while
 * storing the GUID for asset loading. Works with any type T that has
 * a static constexpr const char* kAssetTypeName member.
 *
 * @tparam T The asset type (must expose a static `kAssetTypeName` member)
 */
template <typename T>
struct AssetRef : AssetRefBase
{
    // Get typed pointer (lazy-loads if GUID set but not loaded).
    // Drops the cached pointer when the AssetManager epoch advances so we
    // don't return a dangling reference after asset reloads / hot reloads.
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
            loadAttempted = true;  // Set before loading to prevent retry spam on failure
            ptr = mgr->LoadByGuidAndType(guid, T::kAssetTypeName);
            // Only re-arm the retry when the asset system simply wasn't ready
            // yet (startup race, before the filesystem / GUID table are up). A
            // genuine miss - a deleted or missing asset while the system IS
            // ready - must NOT retry, or it re-opens the missing file every
            // frame and floods the log. If the asset is later (re)imported,
            // InvalidateAsset bumps the epoch and the check above re-arms us.
            if (!ptr && !mgr->IsReady())
            {
                loadAttempted = false;
            }
        }
        return static_cast<T*>(ptr);
    }

    const T* Get() const { return const_cast<AssetRef*>(this)->Get(); }

    // Transparent pointer access
    T* operator->() { return Get(); }
    const T* operator->() const { return Get(); }
    T& operator*() { return *Get(); }
    const T& operator*() const { return *Get(); }

    // Implicit conversion to raw pointer
    operator T*() { return Get(); }
    operator const T*() const { return Get(); }

    // Boolean conversion (triggers lazy-load to check if asset exists)
    operator bool() const { return const_cast<AssetRef*>(this)->Get() != nullptr; }

    // Assignment from raw pointer (used by asset manager)
    AssetRef& operator=(T* p)
    {
        ptr = p;
        auto* mgr = Deki::AssetManager::Get();
        cachedEpoch = mgr ? mgr->GetEpoch() : 0;
        return *this;
    }

    // Comparison operators
    bool operator==(const T* other) const { return Get() == other; }
    bool operator!=(const T* other) const { return Get() != other; }
    bool operator==(std::nullptr_t) const { return ptr == nullptr; }
    bool operator!=(std::nullptr_t) const { return ptr != nullptr; }
};

}  // namespace Deki
