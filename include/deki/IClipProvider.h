#pragma once

#include <cstdint>

/**
 * @file IClipProvider.h
 * @brief Interface for components that provide clip rectangles
 *
 * Implement this interface on any component to have Standard2DRenderer
 * automatically push/pop clip rects during rendering. No pass registration needed.
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
 *     IClipProvider::InterfaceID,
 *     ::Deki::TypeId<MyComponent>(),
 *     [](Component* c) -> void* {
 *         return static_cast<IClipProvider*>(static_cast<MyComponent*>(c));
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

class IClipProvider
{
public:
    static constexpr uint32_t InterfaceID = 0x434C4950;  // "CLIP"

    virtual ~IClipProvider() = default;
    virtual float GetClipWidth() const = 0;
    virtual float GetClipHeight() const = 0;
};

}  // namespace Deki
