#pragma once

#include <deki/Component.h>

// Gets an interface pointer from a component without RTTI or vtable changes.
// Packages register, per component type, a function that turns a Component*
// into an interface pointer. The renderer queries it for IClipProvider,
// ISortableProvider and so on without knowing the concrete class.
//
// In your component .cpp:
//   static struct MyAdapterRegistrar {
//       MyAdapterRegistrar() {
//           ComponentInterfaceAdapters::Register(
//               IClipProvider::kInterfaceID, ::Deki::TypeId<MyComponent>(),
//               [](Component* c) -> void* {
//                   return static_cast<IClipProvider*>(static_cast<MyComponent*>(c));
//               });
//       }
//   } s_reg;

namespace Deki
{

using InterfaceAdapter = void* (*)(Component*);

namespace ComponentInterfaceAdapters
{

/// Registers `adapter` as the way to get interface `interfaceId` from a
/// component of type `componentType`.
void Register(uint32_t interfaceId, ComponentType componentType, InterfaceAdapter adapter);
/// The interface `interfaceId` of `comp`, or nullptr when it has none.
void* Query(uint32_t interfaceId, Component* comp);

/// The adapter Query would call for a component of this type (exact type
/// first, then its base type), or nullptr. A caller can cache it per component
/// type instead of looking it up per component per frame; look again whenever
/// Version() changes.
InterfaceAdapter Find(uint32_t interfaceId, ComponentType componentType, ComponentType baseType);

/// Goes up with every Register. A cache of adapters (the renderer keeps one
/// per component type) compares it to notice adapters registered later, such
/// as by a package that loaded after the cache was filled.
uint32_t Version();

}  // namespace ComponentInterfaceAdapters

}  // namespace Deki
