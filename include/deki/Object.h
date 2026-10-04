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
// The transform width is chosen by the build. DEKI_TRANSFORM_2D gives every
// object x, y, rotation and scale; DEKI_TRANSFORM_3D adds the third axis and
// implies 2D; neither means the object carries no transform at all, which is
// what a project with nothing to draw gets. The generated build sets them from
// what the project's packages declare (deki-editor/COMPATIBILITY.md), and the
// reflection ABI handshake refuses a package DLL built at a different width.
#if defined(DEKI_TRANSFORM_3D) && !defined(DEKI_TRANSFORM_2D)
#error "DEKI_TRANSFORM_3D requires DEKI_TRANSFORM_2D"
#endif

/// A Object's world-space transform, read in one go (see Object::GetWorldTransform).

namespace Deki
{

class Object;

// Engine-internal entry points (src/ObjectInternal.h): the scene loaders and
// the editor create components from a file's type name or a pre-built
// instance. A script adds a component with AddComponent<T>() and nothing else.

namespace Internal
{
void AttachComponent(Object& object, Component* component);
#ifdef DEKI_EDITOR
Component* AddComponentByName(Object& object, const std::string& typeName, const std::string& classHint);
int DetachComponent(Object& object, Component* component);
void ReattachComponent(Object& object, Component* component, size_t index);
void ForgetComponentData(const Component* component);
#endif
}  // namespace Internal

#ifdef DEKI_TRANSFORM_2D
struct WorldTransform
{
    float x;
    float y;
    float rotation;  // radians
    float scaleX;
    float scaleY;
#ifdef DEKI_TRANSFORM_3D
    // Appended, so a reader of the five above sees the same offsets at
    // either width. rotation above is the Z angle; these are X and Y.
    float z;
    float rotationX;
    float rotationY;
    float scaleZ;
#endif
};
#endif

class Object
{
private:
#ifdef DEKI_TRANSFORM_2D
    // World transform cache (computed on demand via dirty flag pattern).
    // Mutable because they're lazily computed in const getters.
    mutable float m_WorldX = 0.0f, m_WorldY = 0.0f;
    mutable float m_WorldRotation = 0.0f;
    mutable float m_WorldScaleX = 1.0f, m_WorldScaleY = 1.0f;
    mutable bool m_WorldTransformDirty = true;
#ifdef DEKI_TRANSFORM_3D
    mutable float m_WorldZ = 0.0f;
    mutable float m_WorldRotationX = 0.0f, m_WorldRotationY = 0.0f;
    mutable float m_WorldScaleZ = 1.0f;
#endif

    /// Ensure world transform is valid (recomputes from parent chain if dirty).
    /// Inline on purpose: the clean case is a flag load in the caller instead
    /// of a call into the engine DLL, and a renderer reads these several times
    /// per object per frame.
    void EnsureWorldTransformValid() const
    {
        if (m_WorldTransformDirty)
        {
            RecomputeWorldTransform();
        }
    }
    void RecomputeWorldTransform() const;

    // Local transform (relative to parent) - use setters to modify.
    // rotation is in radians; the inspector displays degrees.
    float m_X = 0.0f, m_Y = 0.0f;
    float m_Rotation = 0.0f;
    float m_ScaleX = 1.0f, m_ScaleY = 1.0f;
#ifdef DEKI_TRANSFORM_3D
    // The third axis. `rotation` stays the Z angle (COMPATIBILITY.md); these
    // are rotation about X and Y, radians, applied X then Y then Z.
    float m_Z = 0.0f;
    float m_RotationX = 0.0f, m_RotationY = 0.0f;
    float m_ScaleZ = 1.0f;
#endif
#endif  // DEKI_TRANSFORM_2D
    static uint32_t s_NextId;
    uint32_t m_Id;
    std::string m_Name;
    std::vector<Component*> m_Components;
    bool m_Active = true;
    bool m_EditorOnly = false;  // Editor-only objects are not serialized
    bool m_Persistent = false;  // Persist across scene changes

    // Editor-only instance PREDICATES, present in every build so that the class
    // layout does not depend on DEKI_EDITOR. Package DLLs read `parent` and
    // `children` below through inline GetParent()/GetChildren()/GetComponent<T>();
    // when three editor-only members sat here instead, those offsets moved ~72
    // bytes between configurations and the accessors returned garbage with no
    // diagnostic. This byte lands in padding that existed anyway (see the
    // static_asserts in Object.cpp), so it costs the embedded build nothing.
    //
    // The instance PAYLOAD - the source-scene GUID and the overrides JSON - is
    // cold (touched only on expand/collapse, override collect/apply and
    // serialization) and lives in a side table in Object.cpp, so the runtime
    // build carries none of it, not even a pointer.
    static constexpr uint8_t kEditorFlagSceneInstance = 1u << 0;
    static constexpr uint8_t kEditorFlagPartOfInstance = 1u << 1;
    uint8_t m_EditorFlags = 0;

    Scene* m_OwnerScene = nullptr;

    // Hierarchy support
    Object* m_Parent = nullptr;
    std::vector<Object*> m_Children;

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
    /// side table in Object.cpp, which the runtime build must not need to
    /// see. Defining it `= default` in this header would reintroduce exactly the
    /// layout dependency this design removes.
    ~Object();

    uint32_t GetId() const { return m_Id; }
    void SetId(uint32_t newId);
    static uint32_t GenerateId() { return s_NextId++; }
    static uint32_t GenerateDeterministicId(uint32_t seed1, uint32_t seed2)
    {
        uint32_t hash = 2166136261u;
        hash ^= seed1;
        hash *= 16777619u;
        hash ^= seed2;
        hash *= 16777619u;
        if (hash == 0)
        {
            hash = 1;
        }
        return hash;
    }
    const std::string& GetName() const { return m_Name; }
    void SetName(const std::string& newName) { m_Name = newName; }
    bool IsActive() const { return m_Active; }          // Getter for active
    void SetActive(bool value) { m_Active = value; }    // Setter for active
    bool IsEditorOnly() const { return m_EditorOnly; }  // Editor-only objects are not serialized
    void SetEditorOnly(bool value) { m_EditorOnly = value; }
    bool IsPersistent() const { return m_Persistent; }  // Persist across scene changes
    void SetPersistent(bool value) { m_Persistent = value; }
    void SetOwnerScene(Scene* scene) { m_OwnerScene = scene; }
    Scene* GetOwnerScene() const { return m_OwnerScene; }

#ifdef DEKI_EDITOR
    // Scene instance support. Declared editor-only, which is layout-neutral:
    // Object has no virtual functions (asserted in Object.cpp), so
    // conditionally declared non-virtual members change nothing about the object.
    //
    // The predicates stay inline - IsSceneInstance() is called per-object
    // per-frame from HierarchyPanel - and read the flag byte directly. The
    // string accessors are out of line because their storage is the side table.
    bool IsSceneInstance() const { return (m_EditorFlags & kEditorFlagSceneInstance) != 0; }
    bool IsPartOfInstance() const { return (m_EditorFlags & kEditorFlagPartOfInstance) != 0; }
    void SetPartOfInstance(bool value)
    {
        if (value)
        {
            m_EditorFlags |= kEditorFlagPartOfInstance;
        }
        else
        {
            m_EditorFlags &= static_cast<uint8_t>(~kEditorFlagPartOfInstance);
        }
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
    Object* GetParent() const { return m_Parent; }
    const std::vector<Object*>& GetChildren() const { return m_Children; }
    Object* FindChild(uint32_t childId);
    Object* FindChildByName(const std::string& name);
    void RemoveFromParent();
    bool IsDescendantOf(Object* ancestor) const;
    void InsertChildAt(Object* child, size_t index);
    int GetChildIndex(Object* child) const;

    // Transform hierarchy helpers
    bool IsActiveInHierarchy() const;

#ifdef DEKI_TRANSFORM_2D
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
#ifdef DEKI_TRANSFORM_3D
        return { m_WorldX, m_WorldY,         m_WorldRotation,  m_WorldScaleX, m_WorldScaleY,
                 m_WorldZ, m_WorldRotationX, m_WorldRotationY, m_WorldScaleZ };
#else
        return { m_WorldX, m_WorldY, m_WorldRotation, m_WorldScaleX, m_WorldScaleY };
#endif
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
    float GetX() const { return m_X; }
    float GetY() const { return m_Y; }
    float GetLocalRotation() const { return m_Rotation; }
    float GetScaleX() const { return m_ScaleX; }
    float GetScaleY() const { return m_ScaleY; }

    // Single-axis transform setters (auto-update world transform)
    void SetX(float newX);
    void SetY(float newY);
    void SetScaleX(float newScaleX);
    void SetScaleY(float newScaleY);

#ifdef DEKI_TRANSFORM_3D
    // The third axis. Absent at the 2D width, so code that uses these is
    // code that declared transform_3d.
    float GetZ() const { return m_Z; }
    float GetRotationX() const { return m_RotationX; }
    float GetRotationY() const { return m_RotationY; }
    float GetScaleZ() const { return m_ScaleZ; }
    void SetZ(float newZ);
    void SetRotationX(float radians);
    void SetRotationY(float radians);
    void SetScaleZ(float newScaleZ);
    float GetWorldZ() const
    {
        EnsureWorldTransformValid();
        return m_WorldZ;
    }
    float GetWorldRotationX() const
    {
        EnsureWorldTransformValid();
        return m_WorldRotationX;
    }
    float GetWorldRotationY() const
    {
        EnsureWorldTransformValid();
        return m_WorldRotationY;
    }
    float GetWorldScaleZ() const
    {
        EnsureWorldTransformValid();
        return m_WorldScaleZ;
    }
#endif
#else
    // No transform in this build. The hierarchy still calls this when a parent
    // changes, so it stays declared and costs nothing.
    void MarkWorldTransformDirty() {}
#endif  // DEKI_TRANSFORM_2D

    template <typename T, typename... Args>
    T* AddComponent(Args&&... args)
    {
        T* component = new T(std::forward<Args>(args)...);
        // A hand-written class may attach its own entry in its constructor;
        // otherwise the registered (or fallback) entry for T is used.
        if (!component->GetTypeInfo())
        {
            component->SetTypeInfo(TypeInfoFor<T>());
        }
        component->SetOwner(this);
        m_Components.push_back(component);
        return component;
    }

private:
    // Not part of the script API: reached through Deki::Memory::Internal (see the top
    // of this file). A script that needs a component names its type.
    friend void Internal::AttachComponent(Object& object, Component* component);
    void AddComponent(Component* component)
    {
        component->SetOwner(this);
        m_Components.push_back(component);
    }
#ifdef DEKI_EDITOR
    friend Component* Internal::AddComponentByName(Object& object, const std::string& typeName,
                                                   const std::string& classHint);
    friend int Internal::DetachComponent(Object& object, Component* component);
    friend void Internal::ReattachComponent(Object& object, Component* component, size_t index);
    Component* AddComponent(const std::string& typeName, const std::string& classHint);
#endif

public:
    /**
     * @brief Get component by type, including derived types
     * Matches both exact type and components that inherit from T.
     * E.g., GetComponent<SetupComponent>() returns SDCardComponent if it inherits from SetupComponent.
     */
    template <typename T>
    T* GetComponent()
    {
        for (auto* comp : m_Components)
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
        for (auto* comp : m_Components)
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
        for (auto* comp : m_Components)
        {
            if (comp->GetType() == typeId)
            {
                return comp;
            }
        }
        return nullptr;
    }

    const Component* GetComponentByTypeId(uint32_t typeId) const
    {
        for (auto* comp : m_Components)
        {
            if (comp->GetType() == typeId)
            {
                return comp;
            }
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
        for (auto* comp : m_Components)
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
        for (auto* comp : m_Components)
        {
            if (comp->GetType() == TypeId<T>() || comp->GetBaseType() == TypeId<T>())
            {
                return true;
            }
        }
        return false;
    }

    template <typename T>
    void RemoveComponent()
    {
        for (auto it = m_Components.begin(); it != m_Components.end(); ++it)
        {
            if ((*it)->GetType() == TypeId<T>())
            {
                delete *it;
                m_Components.erase(it);
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
        for (auto it = m_Components.begin(); it != m_Components.end(); ++it)
        {
            if (*it == comp)
            {
                delete *it;
                m_Components.erase(it);
                break;
            }
        }
    }

    /**
     * @brief Get all components on this object
     * @return Reference to the vector of components
     */
    const std::vector<Component*>& GetComponents() const { return m_Components; }

    /**
     * @brief Find a component implementing a given interface
     * No RTTI needed - uses ComponentInterfaceAdapters registry.
     * T must have a static constexpr uint32_t kInterfaceID member.
     */
    template <typename T>
    T* FindInterface()
    {
        for (auto* comp : m_Components)
        {
            void* iface = ComponentInterfaceAdapters::Query(T::kInterfaceID, comp);
            if (iface)
            {
                return static_cast<T*>(iface);
            }
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
    if (!root)
    {
        return;
    }
    fn(root);
    for (Object* child : root->GetChildren())
    {
        VisitSubtree(child, fn);
    }
}

/// Like VisitSubtree, but does not enter inactive objects (nor visit them).
template <typename Fn>
inline void VisitActiveSubtree(Object* root, Fn&& fn)
{
    if (!root || !root->IsActive())
    {
        return;
    }
    fn(root);
    for (Object* child : root->GetChildren())
    {
        VisitActiveSubtree(child, fn);
    }
}

/// First object in pre-order for which `pred(Object*)` is true, or null.
template <typename Pred>
inline Object* FindInSubtree(Object* root, Pred&& pred)
{
    if (!root)
    {
        return nullptr;
    }
    if (pred(root))
    {
        return root;
    }
    for (Object* child : root->GetChildren())
    {
        if (Object* found = FindInSubtree(child, pred))
        {
            return found;
        }
    }
    return nullptr;
}

}  // namespace Deki
