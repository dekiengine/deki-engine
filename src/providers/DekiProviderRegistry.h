#pragma once

#include "DekiProviderRegistryBase.h"
#include <deki/providers/IPackage.h>
#include <type_traits>

// Typed, header-only wrapper around ProviderRegistryBase.
//
// Each multi-provider package aliases this template to expose its registry
// with two access methods:
//
//   namespace Deki { using Location = ProviderRegistry<IDekiLocationProvider>; }
//
//   auto* p = Deki::Location::GetProvider<BeaconDBLocationProvider>();   // typed
//   auto* d = Deki::Location::GetDefault();                              // first registered
//
// There is no GetProviderById(const char*): a provider is picked either by
// type, checked at compile time, or as the default. Lookups by string invite
// "if A fails, try B" fallbacks, which the project's rules forbid.
//
// Interface contract:
//   - Interface must derive from IPackage.
//   - Interface must define `static constexpr const char* Category` matching
//     the category string used by ForCategory().
//   - Each concrete provider T must define `static constexpr const char* ProviderId`.
//
// No RTTI: lookups compare ProviderId strings, and the static_cast to T* is
// safe because each (category, ProviderId) pair belongs to exactly one type.
// Builds under -fno-rtti.

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

    /// The first provider registered, or nullptr when there is none.
    /// Registration order is the order of the Register() calls in the
    /// package's DekiPluginRegisterComponents, so source order picks the
    /// default.
    static Interface* GetDefault() { return static_cast<Interface*>(Base().GetDefault()); }

    // --- Diagnostics-only enumeration. Do not use for selection logic. ---

    static int GetProviderCount() { return Base().GetCount(); }

    static Interface* GetProviderAt(int idx) { return static_cast<Interface*>(Base().GetAt(idx)); }

    // --- Registration. Called by the owning package's DekiPluginRegisterComponents. ---

    /// Registers a provider owned by a plugin DLL. The registry takes ownership
    /// and deletes it in ClearPluginProviders(), which PackageLoader calls
    /// before FreeLibrary.
    static void RegisterPlugin(Interface* provider)
    {
        Base().Register(provider, provider->GetPackageId(), /*isPlugin=*/true);
    }

    /// Registers a provider owned by the engine core (rare; packages normally
    /// use RegisterPlugin). The registry never deletes these.
    static void Register(Interface* provider)
    {
        Base().Register(provider, provider->GetPackageId(), /*isPlugin=*/false);
    }

    static void Unregister(Interface* provider) { Base().Unregister(provider); }
};

}  // namespace Deki
