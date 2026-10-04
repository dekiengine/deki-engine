#pragma once

#include <cstdint>
#include <deki/Object.h>

// ObjectRefBase is the type-erased part of ObjectRef, which the engine uses.

namespace Deki
{

struct ObjectRefBase
{
    uint32_t id = 0;
    uint32_t componentTypeId = 0;  // 0 = first component of type T, nonzero = that component type
    Object* resolved = nullptr;

    bool IsValid() const { return id != 0; }
    bool IsResolved() const { return resolved != nullptr; }

    /// Points the reference at `obj` at runtime. nullptr clears it.
    void Set(Object* obj)
    {
        id = obj ? obj->GetId() : 0;
        componentTypeId = 0;
        resolved = obj;
    }

    /// Points the reference at the component of type `typeId` on `obj`.
    /// nullptr clears it.
    void Set(Object* obj, uint32_t typeId)
    {
        id = obj ? obj->GetId() : 0;
        componentTypeId = typeId;
        resolved = obj;
    }
};

/// An object reference the user assigns in the editor.
///
/// The property generator recognizes ObjectRef<T>, and the inspector draws an
/// object picker filtered to objects with component T. The engine resolves the
/// reference before Start(), so Get() returns the component directly.
///
/// When componentTypeId is set, Get() returns the component with that type id.
/// When it is 0, Get() returns the first component of type T.
template <typename T>
struct ObjectRef : ObjectRefBase
{
    // The resolved component, or nullptr when unresolved or the object lacks it.
    T* Get() const
    {
        if (!resolved)
        {
            return nullptr;
        }
        if (componentTypeId != 0)
        {
            Component* comp = resolved->GetComponentByTypeId(componentTypeId);
            return comp ? static_cast<T*>(comp) : nullptr;
        }
        return resolved->GetComponent<T>();
    }

    // The resolved object, or nullptr when unresolved.
    Object* GetObject() const { return resolved; }

    operator uint32_t() const { return id; }

    // Sets a bare object id and clears componentTypeId and the resolved object.
    ObjectRef& operator=(uint32_t newId)
    {
        id = newId;
        componentTypeId = 0;
        resolved = nullptr;
        return *this;
    }

    bool operator==(const ObjectRef& other) const { return id == other.id && componentTypeId == other.componentTypeId; }
    bool operator!=(const ObjectRef& other) const { return !(*this == other); }

    T* operator->() const { return Get(); }

    // True when the reference is set and resolved, so safe to dereference.
    explicit operator bool() const { return id != 0 && resolved != nullptr; }
};

}  // namespace Deki
