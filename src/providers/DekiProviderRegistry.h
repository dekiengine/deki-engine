#pragma once

#include "DekiProviderRegistryBase.h"
#include <deki/providers/IPackage.h>
#include <type_traits>

/**
 * @brief Header-only typed wrapper around ProviderRegistryBase.
 *
 * Each multi-provider package aliases this template to expose its registry
 * with two access methods only:
 *
 *   namespace Deki { using Location = ProviderRegistry<IDekiLocationProvider>; }
 *
 *   auto* p = Deki::Location::GetProvider<BeaconDBLocationProvider>();   // typed
 *   auto* d = Deki::Location::GetDefault();                              // first registered
 *
 * No GetProviderById(const char*) is exposed - selection is always either
 * compile-time-checked (typed) or convention-driven (default). String-keyed
 * runtime lookups invite the "if A fails, try B" fallback pattern that the
 * project's NEVER USE FALLBACKS rule forbids.
 *
 * Interface contract:
 *   - Interface must derive from IPackage.
 *   - Interface must define `static constexpr const char* Category` matching
 *     the category string used by ForCategory().
 *   - Each concrete provider T must define `static constexpr const char* ProviderId`.
 *
 * RTTI: not used. Lookups go through string comparison on ProviderId, and the
 * static_cast to T* is safe because the (category, ProviderId) pair is owned
 * by exactly one type. Builds cleanly under -fno-rtti.
 */

namespace Deki
{

template <typename Interface>
class ProviderRegistry
{
    static_assert(std::is_base_of<IPackage, Interface>::value, "Interface must derive from IDekiPackage.");

    static ProviderRegistryBase& Base() { return ProviderRegistryBase::ForCategory(Interface::Category); }

public:
    /// Typed access. T must derive from Interface and define ProviderId.
    /// Returns nullptr if no provider with T::ProviderId has registered.
    template <typename T>
    static T* GetProvider()
    {
        static_assert(std::is_base_of<Interface, T>::value, "T must derive from Interface.");
        return static_cast<T*>(Base().GetById(T::ProviderId));
    }

    /// First-registered provider, or nullptr if the registry is empty.
    /// Registration order is the order of calls to Register() at
    /// DekiPluginRegisterComponents time - i.e. source-code order in the
    /// package that registers them. This is the only "default" knob.
    static Interface* GetDefault() { return static_cast<Interface*>(Base().GetDefault()); }

    // --- Diagnostics-only enumeration. Do not use for selection logic. ---

    static int GetProviderCount() { return Base().GetCount(); }

    static Interface* GetProviderAt(int idx) { return static_cast<Interface*>(Base().GetAt(idx)); }

    // --- Registration. Called by the owning package's DekiPluginRegisterComponents. ---

    /// Register a provider owned by a plugin DLL. The registry takes ownership
    /// and will delete the pointer during ClearPluginProviders() (called by
    /// PackageLoader before FreeLibrary).
    static void RegisterPlugin(Interface* provider)
    {
        Base().Register(provider, provider->GetPackageId(), /*isPlugin=*/true);
    }

    /// Register a provider owned by engine-core (rare; typically packages use
    /// the plugin variant). The registry never deletes these.
    static void Register(Interface* provider)
    {
        Base().Register(provider, provider->GetPackageId(), /*isPlugin=*/false);
    }

    static void Unregister(Interface* provider) { Base().Unregister(provider); }
};

}  // namespace Deki
