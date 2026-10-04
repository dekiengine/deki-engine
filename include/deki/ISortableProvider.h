#pragma once

#include <cstdint>

// A component that implements ISortableProvider has its object sorted by
// Standard2DRenderer by GetSortingOrder().
//
// The renderer finds the interface through ComponentInterfaceAdapters, so the
// component must also register an adapter, once, from the package's
// registration path:
//
//   #include "ComponentInterfaceAdapters.h"
//
//   ComponentInterfaceAdapters::Register(
//       ISortableProvider::kInterfaceID,
//       ::Deki::TypeId<MyComponent>(),
//       [](Component* c) -> void* {
//           return static_cast<ISortableProvider*>(static_cast<MyComponent*>(c));
//       });
//
// Component has no QueryInterface method; the adapter registry replaces it.

namespace Deki
{

class ISortableProvider
{
public:
    static constexpr uint32_t kInterfaceID = 0x534F5254;  // "SORT"

    virtual ~ISortableProvider() = default;
    virtual int32_t GetSortingOrder() const = 0;
};

}  // namespace Deki
