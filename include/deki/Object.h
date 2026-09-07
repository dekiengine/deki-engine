#pragma once
#include <stdint.h>

#include <vector>
#include <string>
#include <algorithm>

#include <deki/Component.h>
#include <deki/reflection/ComponentFactory.h>
#include <deki/ComponentInterfaceAdapters.h>
#include <deki/Math.h>

/**
 * @brief Entity class - a container for components
 */
/// A Object's world-space transform, read in one go (see Object::GetWorldTransform).

namespace Deki
{

struct WorldTransform
{
    float x;
    float y;
    float rotation;  // radians
    float scaleX;
    float scaleY;
};

class Object
{
private:
    // World transform cache (computed on demand via dirty flag pattern).
    // Mutable because they're lazily computed in const getters.
    mutable float m_WorldX = 0.0f, m_WorldY = 0.0f;
    mutable float m_WorldRotation = 0.0f;
    mutable float m_WorldScaleX = 1.0f, m_WorldScaleY = 1.0f;
    mutable bool m_WorldTransformDirty = true;

    /// Ensure world transform is valid (recomputes from parent chain if dirty).
    /// Inline on purpose: the clean case is a flag load in the caller instead
    /// of a call into the engine DLL, and a renderer reads these several times
    /// per object per frame.
    void EnsureWorldTransformValid() const
    {
        if (m_WorldTransformDirty)
            RecomputeWorldTransform();
    }
    void RecomputeWorldTransform() const;

    // Local transform (relative to parent) - use setters to modify.
    // rotation is in radians; the inspector displays degrees.
    float x = 0.0f, y = 0.0f;
    float rotation = 0.0f;
    float m_ScaleX = 1.0f, m_ScaleY = 1.0f;
    static uint32_t s_NextId;
    uint32_t id;
    std::string name;
    std::vector<Component*> components;
    bool active = true;
    bool m_EditorOnly = false;  // Editor-only objects are not serialized
    bool m_Persistent = false;  // Persist across scene changes

    // Editor-only instance PREDICATES, present in every build so that the class
    // layout does not depend on DEKI_EDITOR. Package DLLs read `parent` and
    // `children` below through inline GetParent()/GetChildren()/GetComponent<T>();
    // when three editor-only members sat here instead, those offsets moved ~72
    // bytes between configurations and the accessors returned garbage with no
    // diagnostic. This byte lands in padding that existed anyway (see the
    // static_asserts in DekiObject.cpp), so it costs the embedded build nothing.
    //
    // The instance PAYLOAD — the source-scene GUID and the overrides JSON — is
    // cold (touched only on expand/collapse, override collect/apply and
    // serialization) and lives in a side table in DekiObject.cpp, so the runtime
    // build carries none of it, not even a pointer.
    static constexpr uint8_t EditorFlag_SceneInstance = 1u << 0;
    static constexpr uint8_t EditorFlag_PartOfInstance = 1u << 1;
    uint8_t m_EditorFlags = 0;

    Scene* ownerScene = nullptr;

    // Hierarchy support
    Object* parent = nullptr;
    std::vector<Object*> children;

public:
    Object();
    Object(const std::string& objectName);

    /// Non-copyable: a Object owns its children and components by raw
    /// pointer, so a copy would double-free them. Made explicit here because the
    /// editor-only instance payload lives in a side table keyed by `this`, which
    /// a copy would silently share.
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;

    /// Out of line ON PURPOSE. It erases this object's row from the editor-only
    /// side table in DekiObject.cpp, which the runtime build must not need to
    /// see. Defining it `= default` in this header would reintroduce exactly the
    /// layout dependency this design removes.
    ~Object();

    uint32_t GetId() const { return id; }
    void SetId(uint32_t newId);
    static uint32_t GenerateId() { return s_NextId++; }
    static uint32_t GenerateDeterministicId(uint32_t seed1, uint32_t seed2)
    {
        uint32_t hash = 2166136261u;
        hash ^= seed1;
        hash *= 16777619u;
        hash ^= seed2;
        hash *= 16777619u;
        if (hash == 0) hash = 1;
        return hash;
    }
    const std::string& GetName() const { return name; }
    void SetName(const std::string& newName) { name = newName; }
    bool IsActive() const { return active; }            // Getter for active
    void SetActive(bool value) { active = value; }      // Setter for active
    bool IsEditorOnly() const { return m_EditorOnly; }  // Editor-only objects are not serialized
    void SetEditorOnly(bool value) { m_EditorOnly = value; }
    bool IsPersistent() const { return m_Persistent; }  // Persist across scene changes
    void SetPersistent(bool value) { m_Persistent = value; }
    void SetOwnerScene(Scene* scene) { ownerScene = scene; }
    Scene* GetOwnerScene() const { return ownerScene; }

#ifdef DEKI_EDITOR
    // Scene instance support. Declared editor-only, which is layout-neutral:
    // Object has no virtual functions (asserted in DekiObject.cpp), so
    // conditionally declared non-virtual members change nothing about the object.
    //
    // The predicates stay inline — IsSceneInstance() is called per-object
    // per-frame from HierarchyPanel — and read the flag byte directly. The
    // string accessors are out of line because their storage is the side table.
    bool IsSceneInstance() const { return (m_EditorFlags & EditorFlag_SceneInstance) != 0; }
    bool IsPartOfInstance() const { return (m_EditorFlags & EditorFlag_PartOfInstance) != 0; }
    void SetPartOfInstance(bool value)
    {
        if (value)
            m_EditorFlags |= EditorFlag_PartOfInstance;
        else
            m_EditorFlags &= static_cast<uint8_t>(~EditorFlag_PartOfInstance);
    }
    /// Empty when this object is not a scene instance. The reference is valid
    /// until the next SetSourceSceneGuid on this object, or its destruction.
    const std::string& GetSourceSceneGuid() const;
    void SetSourceSceneGuid(const std::string& guid);
    const std::string& GetInstanceOverridesJson() const;
    void SetInstanceOverridesJson(const std::string& json);
#endif

    // Hierarchy management
    void AddChild(Object* child);
    void RemoveChild(Object* child);
    void SetParent(Object* newParent);
    Object* GetParent() const { return parent; }
    const std::vector<Object*>& GetChildren() const { return children; }
    Object* FindChild(uint32_t childId);
    Object* FindChildByName(const std::string& name);
    void RemoveFromParent();
    bool IsDescendantOf(Object* ancestor) const;
    void InsertChildAt(Object* child, size_t index);
    int GetChildIndex(Object* child) const;

    // Transform hierarchy helpers
    bool IsActiveInHierarchy() const;

    void SetLocalPosition(float newX, float newY);
    void SetWorldPosition(float worldX, float worldY);

    // Pixel-input convenience: takes screen-pixel-space inputs, divides by
    // EngineSettings::Global().pixelsPerMeter and stores as meters.
    void SetWorldPositionPx(float pxX, float pxY);
    void SetScale(float scaleX, float scaleY);

    /// Rotation setter. Angle is in radians (the inspector displays
    /// degrees via DEKI_UNIT(Angle)).
    void SetRotation(float radians);


    // World transform getters (lazy evaluation - recomputes if dirty)

    /// All five world-transform values with one dirty check.
    WorldTransform GetWorldTransform() const
    {
        EnsureWorldTransformValid();
        return { m_WorldX, m_WorldY, m_WorldRotation, m_WorldScaleX, m_WorldScaleY };
    }
    float GetWorldX() const
    {
        EnsureWorldTransformValid();
        return m_WorldX;
    }
    float GetWorldY() const
    {
        EnsureWorldTransformValid();
        return m_WorldY;
    }
    float GetWorldRotation() const
    {
        EnsureWorldTransformValid();
        return m_WorldRotation;
    }
    float GetWorldScaleX() const
    {
        EnsureWorldTransformValid();
        return m_WorldScaleX;
    }
    float GetWorldScaleY() const
    {
        EnsureWorldTransformValid();
        return m_WorldScaleY;
    }

    /// Mark this object's world transform (and all descendants) as needing recalculation
    void MarkWorldTransformDirty();

    // Local transform getters
    float GetX() const { return x; }
    float GetY() const { return y; }
    float GetLocalRotation() const { return rotation; }
    float GetScaleX() const { return m_ScaleX; }
    float GetScaleY() const { return m_ScaleY; }

    // Single-axis transform setters (auto-update world transform)
    void SetX(float newX);
    void SetY(float newY);
    void SetScaleX(float newScaleX);
    void SetScaleY(float newScaleY);

    template <typename T, typename... Args>
    T* AddComponent(Args&&... args)
    {
        T* component = new T(std::forward<Args>(args)...);
        component->SetTypeInfo(TypeInfoFor<T>());
        component->SetOwner(this);
        components.push_back(component);
        return component;
    }

    /**
     * @brief Add a pre-created component (runtime scene loading)
     * @param component The component to add (takes ownership)
     */
    void AddComponent(Component* component)
    {
        component->SetOwner(this);
        components.push_back(component);
    }

#ifdef DEKI_EDITOR
    /**
     * @brief Add a component by type name (editor only)
     * @param typeName The registered component type name
     * @return The created component, or nullptr if type not found
     */
    Component* AddComponent(const std::string& typeName);
    Component* AddComponent(const std::string& typeName, const std::string& classHint);
#endif
    /**
     * @brief Get component by type, including derived types
     * Matches both exact type and components that inherit from T.
     * E.g., GetComponent<SetupComponent>() returns SDCardComponent if it inherits from SetupComponent.
     */
    template <typename T>
    T* GetComponent()
    {
        for (auto* comp : components)
        {
            if (comp->GetType() == TypeId<T>() || comp->GetBaseType() == TypeId<T>())
            {
                return static_cast<T*>(comp);
            }
        }
        return nullptr;
    }

    template <typename T>
    const T* GetComponent() const
    {
        for (auto* comp : components)
        {
            if (comp->GetType() == TypeId<T>() || comp->GetBaseType() == TypeId<T>())
            {
                return static_cast<const T*>(comp);
            }
        }
        return nullptr;
    }

    /**
     * @brief Get component by runtime type ID (exact match only)
     */
    Component* GetComponentByTypeId(uint32_t typeId)
    {
        for (auto* comp : components)
        {
            if (comp->GetType() == typeId)
                return comp;
        }
        return nullptr;
    }

    const Component* GetComponentByTypeId(uint32_t typeId) const
    {
        for (auto* comp : components)
        {
            if (comp->GetType() == typeId)
                return comp;
        }
        return nullptr;
    }

    /**
     * @brief Get all components of a specific type, including derived types
     * Matches both exact type and components that inherit from T.
     */
    template <typename T>
    std::vector<T*> GetComponents()
    {
        std::vector<T*> result;
        for (auto* comp : components)
        {
            if (comp->GetType() == TypeId<T>() || comp->GetBaseType() == TypeId<T>())
            {
                result.push_back(static_cast<T*>(comp));
            }
        }
        return result;
    }

    /**
     * @brief Check if object has component of type T, including derived types
     */
    template <typename T>
    bool HasComponent() const
    {
        for (auto* comp : components)
        {
            if (comp->GetType() == TypeId<T>() || comp->GetBaseType() == TypeId<T>()) return true;
        }
        return false;
    }

    template <typename T>
    void RemoveComponent()
    {
        for (auto it = components.begin(); it != components.end(); ++it)
        {
            if ((*it)->GetType() == TypeId<T>())
            {
                delete *it;
                components.erase(it);
                break;
            }
        }
    }

    /**
     * @brief Remove a specific component by pointer
     * @param comp The component to remove
     */
    void RemoveComponent(Component* comp)
    {
        for (auto it = components.begin(); it != components.end(); ++it)
        {
            if (*it == comp)
            {
                delete *it;
                components.erase(it);
                break;
            }
        }
    }

    /**
     * @brief Get all components on this object
     * @return Reference to the vector of components
     */
    const std::vector<Component*>& GetComponents() const { return components; }

    /**
     * @brief Find a component implementing a given interface
     * No RTTI needed — uses ComponentInterfaceAdapters registry.
     * T must have a static constexpr uint32_t InterfaceID member.
     */
    template <typename T>
    T* FindInterface()
    {
        for (auto* comp : components)
        {
            void* iface = ComponentInterfaceAdapters::Query(T::InterfaceID, comp);
            if (iface) return static_cast<T*>(iface);
        }
        return nullptr;
    }
};

// ============================================================================
// Subtree walks
// ============================================================================
// Plain templates, so the visitor is a normal lambda that inlines: the engine
// used to build a std::function recursive lambda for each of these walks (a
// heap allocation plus an indirect call per node) in paths that run on every
// object add, every scene load and, for the camera search, every frame.

/// Pre-order visit of `root` and every descendant. `fn(Object*)`.
template <typename Fn>
inline void VisitSubtree(Object* root, Fn&& fn)
{
    if (!root) return;
    fn(root);
    for (Object* child : root->GetChildren())
        VisitSubtree(child, fn);
}

/// Like VisitSubtree, but does not enter inactive objects (nor visit them).
template <typename Fn>
inline void VisitActiveSubtree(Object* root, Fn&& fn)
{
    if (!root || !root->IsActive()) return;
    fn(root);
    for (Object* child : root->GetChildren())
        VisitActiveSubtree(child, fn);
}

/// First object in pre-order for which `pred(Object*)` is true, or null.
template <typename Pred>
inline Object* FindInSubtree(Object* root, Pred&& pred)
{
    if (!root) return nullptr;
    if (pred(root)) return root;
    for (Object* child : root->GetChildren())
        if (Object* found = FindInSubtree(child, pred))
            return found;
    return nullptr;
}

}  // namespace Deki
