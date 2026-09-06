#pragma once

#include <deki/Component.h>

/**
 * @file ComponentInterfaceAdapters.h
 * @brief Registry for extracting interfaces from components without RTTI or vtable changes
 *
 * Packages register adapter functions that convert a Component* to an interface pointer
 * based on the component's StaticType. The renderer queries this to find IClipProvider,
 * ISortableProvider, etc. without knowing the concrete component class.
 *
 * Usage:
 * @code
 * // In your component .cpp:
 * #include "ComponentInterfaceAdapters.h"
 * static struct MyAdapterRegistrar {
 *     MyAdapterRegistrar() {
 *         ComponentInterfaceAdapters::Register(
 *             IClipProvider::InterfaceID, MyComponent::StaticType,
 *             [](Component* c) -> void* {
 *                 return static_cast<IClipProvider*>(static_cast<MyComponent*>(c));
 *             });
 *     }
 * } s_reg;
 * @endcode
 */

namespace Deki
{


using InterfaceAdapter = void* (*)(Component*);

namespace ComponentInterfaceAdapters
{

void Register(uint32_t interfaceId, ComponentType componentType, InterfaceAdapter adapter);
void* Query(uint32_t interfaceId, Component* comp);

/// The adapter Query would call for a component of this type (exact type
/// first, then its base type), or nullptr. Lets a caller cache the function
/// pointer per component type instead of hashing per component per frame;
/// re-check whenever Version() changes.
InterfaceAdapter Find(uint32_t interfaceId, ComponentType componentType, ComponentType baseType);

/// Bumped by every Register. A cache of Query results (the renderer keeps one
/// per component type) compares it to notice adapters registered later, e.g.
/// by a package that loaded after the cache was filled.
uint32_t Version();

}  // namespace ComponentInterfaceAdapters

}  // namespace Deki
