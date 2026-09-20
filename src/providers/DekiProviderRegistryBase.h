#pragma once

#include <deki/providers/IPackage.h>
#include <vector>
#include <cstddef>

#ifdef _WIN32
#ifdef DEKI_ENGINE_EXPORTS
#define PROVIDER_REGISTRY_API __declspec(dllexport)
#else
#define PROVIDER_REGISTRY_API __declspec(dllimport)
#endif
#else
#define PROVIDER_REGISTRY_API
#endif

/**
 * @brief Per-category provider registry that lives in deki-engine-core.dll.
 *
 * One instance per category string (e.g. "location", "weather"). Package DLLs
 * register their concrete providers here at DekiPlugin_RegisterComponents time.
 *
 * The base stores IPackage* pointers and is interface-agnostic on purpose:
 * the typed wrapper (ProviderRegistry<Interface>) downcasts via static_cast.
 * Safe because each (category, providerId) pair is owned by exactly one type.
 *
 * Same DLL-boundary pattern as SettingsRegistry - singleton symbol is
 * exported from deki-engine-core.dll so every package DLL sees the same
 * instance via dllimport. Plugin-aware ClearPlugin path lets PackageLoader
 * drop providers before FreeLibrary without dangling vtables.
 */

namespace Deki
{

class PROVIDER_REGISTRY_API ProviderRegistryBase
{
public:
    static ProviderRegistryBase& ForCategory(const char* category);

    void Register(IPackage* provider, const char* providerId, bool isPlugin);
    void Unregister(IPackage* provider);

    IPackage* GetById(const char* providerId) const;
    IPackage* GetDefault() const;
    int GetCount() const;
    IPackage* GetAt(int idx) const;

    /// Called by PackageLoader before FreeLibrary on each plugin DLL. Removes
    /// every provider registered with isPlugin=true and frees the owned
    /// memory. Engine-core-owned providers (isPlugin=false) are preserved.
    void ClearPluginProviders();

    /// Static convenience: clear plugin-owned providers across every known
    /// category. Used by PackageLoader::UnloadAllPackages as belt-and-braces
    /// in case a plugin's DekiPlugin_Shutdown didn't clean up after itself.
    static void ClearAllPluginProviders();

    ProviderRegistryBase() = default;
    ProviderRegistryBase(const ProviderRegistryBase&) = delete;
    ProviderRegistryBase& operator=(const ProviderRegistryBase&) = delete;

private:
    struct Entry
    {
        IPackage* provider;
        const char* providerId;  // not owned; concrete provider's static constexpr
        bool isPlugin;
    };
    std::vector<Entry> m_Entries;
};

}  // namespace Deki
