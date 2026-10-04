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

// Provider registry for one category, living in deki-engine-core.dll.
//
// One instance per category string (such as "location" or "weather"). Package
// DLLs register their providers here in DekiPluginRegisterComponents.
//
// The base stores IPackage* and knows no interface, on purpose: the typed
// wrapper (ProviderRegistry<Interface>) downcasts with static_cast. That is
// safe because each (category, providerId) pair belongs to exactly one type.
//
// Like SettingsRegistry, the singleton is exported from deki-engine-core.dll
// so every package DLL sees the same instance. ClearPluginProviders lets
// PackageLoader drop a plugin's providers before FreeLibrary, so no vtable is
// left dangling.

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
    /// every provider registered with isPlugin=true and frees its memory.
    /// Providers owned by the engine core (isPlugin=false) stay.
    void ClearPluginProviders();

    /// Clears plugin-owned providers in every category.
    /// PackageLoader::UnloadAllPackages calls it in case a plugin's
    /// DekiPluginShutdown did not clean up after itself.
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
