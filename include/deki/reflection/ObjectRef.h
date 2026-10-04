#pragma once

#include <cstdint>
#include <deki/Object.h>

/**
 * @brief Base class for type-erased ObjectRef access (used by engine)
 */

namespace Deki
{

struct ObjectRefBase
{
    uint32_t id = 0;
    uint32_t componentTypeId = 0;  // 0 = any (backward compat), nonzero = specific component type
    Object* resolved = nullptr;

    bool IsValid() const { return id != 0; }
    bool IsResolved() const { return resolved != nullptr; }

    /**
     * @brief Set the reference to an object (runtime assignment)
     * @param obj The object to reference, or nullptr to clear
     */
    void Set(Object* obj)
    {
        id = obj ? obj->GetId() : 0;
        componentTypeId = 0;
        resolved = obj;
    }

    /**
     * @brief Set the reference to a specific component on an object
     * @param obj The object to reference, or nullptr to clear
     * @param typeId The component type ID to target
     */
    void Set(Object* obj, uint32_t typeId)
    {
        id = obj ? obj->GetId() : 0;
        componentTypeId = typeId;
        resolved = obj;
    }
};

/**
 * @brief Object reference wrapper for editor-assignable object IDs
 *
 * ObjectRef<T> provides a uint32_t object ID that the property generator
 * recognizes as an ObjectRef type, causing the inspector to draw an object
 * picker dropdown filtered by component type T.
 *
 * The engine automatically resolves ObjectRef references before Start() is called,
 * so you can use Get() to retrieve the component directly.
 *
 * When componentTypeId is set, Get() returns the specific component matching
 * that type ID. When componentTypeId is 0, Get() returns the first component
 * matching template type T (backward compatible behavior).
 *
 * @tparam T The component type to filter by in the object picker
 */
template <typename T>
struct ObjectRef : ObjectRefBase
{
    // Get the resolved component (nullptr if not resolved or object doesn't have component)
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

    // Get the resolved object (nullptr if not resolved)
    Object* GetObject() const { return resolved; }

    // Implicit conversion to uint32_t for runtime use
    operator uint32_t() const { return id; }

    // Assignment from uint32_t (legacy format, clears componentTypeId)
    ObjectRef& operator=(uint32_t newId)
    {
        id = newId;
        componentTypeId = 0;
        resolved = nullptr;
        return *this;
    }

    // Comparison with other ObjectRef
    bool operator==(const ObjectRef& other) const { return id == other.id && componentTypeId == other.componentTypeId; }
    bool operator!=(const ObjectRef& other) const { return !(*this == other); }

    // Pointer-like access to the resolved component
    T* operator->() const { return Get(); }

    // Check if reference is set AND resolved (safe to dereference)
    explicit operator bool() const { return id != 0 && resolved != nullptr; }
};

}  // namespace Deki
