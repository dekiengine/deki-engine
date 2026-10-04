#pragma once

#include <cstdint>

/**
 * @file ISortableProvider.h
 * @brief Interface for components that provide sorting order
 *
 * Implement this interface on any component to have Standard2DRenderer
 * automatically include the object in sorting. No callback registration needed.
 *
 * Implementing the interface is not enough on its own: the renderer discovers
 * it through ComponentInterfaceAdapters, so each component must also register an
 * adapter from its StaticType to this interface. Do that once, from the
 * package's registration path:
 *
 * @code
 * #include "ComponentInterfaceAdapters.h"
 *
 * ComponentInterfaceAdapters::Register(
 *     ISortableProvider::InterfaceID,
 *     ::Deki::TypeId<MyComponent>(),
 *     [](Component* c) -> void* {
 *         return static_cast<ISortableProvider*>(static_cast<MyComponent*>(c));
 *     });
 * @endcode
 *
 * This block used to show a `QueryInterface` override calling
 * `Component::QueryInterface(id)`. Component has no such method and
 * never has - following the example produced a compile error on the first
 * custom implementation. The adapter registry exists precisely so Component
 * needs no virtual for this.
 */

namespace Deki
{

class ISortableProvider
{
public:
    static constexpr uint32_t InterfaceID = 0x534F5254;  // "SORT"

    virtual ~ISortableProvider() = default;
    virtual int32_t GetSortingOrder() const = 0;
};

}  // namespace Deki
