#pragma once

#include <cstdint>

// A component that implements IClipProvider gets its clip rectangle pushed
// and popped by Standard2DRenderer while it renders.
//
// The renderer finds the interface through ComponentInterfaceAdapters, so the
// component must also register an adapter, once, from the package's
// registration path:
//
//   #include "ComponentInterfaceAdapters.h"
//
//   ComponentInterfaceAdapters::Register(
//       IClipProvider::kInterfaceID,
//       ::Deki::TypeId<MyComponent>(),
//       [](Component* c) -> void* {
//           return static_cast<IClipProvider*>(static_cast<MyComponent*>(c));
//       });
//
// Component has no QueryInterface method; the adapter registry replaces it.

namespace Deki
{

class IClipProvider
{
public:
    static constexpr uint32_t kInterfaceID = 0x434C4950;  // "CLIP"

    virtual ~IClipProvider() = default;
    virtual float GetClipWidth() const = 0;
    virtual float GetClipHeight() const = 0;
};

}  // namespace Deki
