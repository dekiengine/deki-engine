#pragma once

#include <cstdint>
#include <unordered_map>

#include <deki/TypeId.h>

namespace Deki
{

// Forward declarations
class Object;
class Behaviour;
class Component;
class Scene;
struct FieldRef;
struct ComponentMeta;
namespace SceneFormat
{
class SceneMsgPackParser;
}

/**
 * @brief Everything the engine knows about one component class.
 *
 * One of these exists per reflected class, emitted by the reflection codegen
 * into the owning package's generated sources and registered with
 * ComponentFactory at startup. A component instance points at its class's
 * entry (Component::GetTypeInfo), which is how GetType(), Deserialize() and
 * the reference fixups work without the class declaring anything itself.
 *
 * All strings are literals with static storage.
 */
struct ComponentTypeInfo
{
    const char* name;           // fully qualified C++ name: the on-disk key; HashString(name) == typeId
    const char* shortName;      // the identifier alone, for display and the scene file's "class" hint
    ComponentType typeId;       // TypeId<T>()
    ComponentType baseTypeId;   // TypeId of the direct component base (Deki::Behaviour, Deki::Component, or a user base)
    bool isAbstract;            // no create()
    bool declaresUpdate;        // the class (or a reflected base) declares Update(); see Component::DeclaresUpdate

    Component* (*create)();     // new instance with its type info set; nullptr when abstract
    bool (*deserialize)(Component* self, SceneFormat::SceneMsgPackParser& parser, uint32_t mapSize);
    void (*resolveObjectRefs)(Component* self, Scene* scene);
    void (*remapObjectRefs)(Component* self, const std::unordered_map<uint32_t, uint32_t>& idRemap);

    const FieldRef* fields;     // flattened field table (own + inherited), every platform
    int fieldCount;

    const ComponentMeta* meta;  // editor-only rich metadata; nullptr on device builds
};

/**
 * @brief Base class for all components (data-only)
 *
 * A component is any class deriving publicly from Component (or Behaviour, for
 * lifecycle callbacks). Nothing else is declared: the reflection codegen finds
 * the class by its base, its identity is its qualified name (see TypeId.h) and
 * the per-class operations below run through the ComponentTypeInfo the engine
 * attaches when the instance is created (AddComponent<T>, the scene loader,
 * the factory).
 *
 * Component provides:
 * - Runtime type identification via GetType()/GetBaseType()
 * - Owner reference for accessing the parent Object
 *
 * For components that need lifecycle methods (PreRender, Start, Update),
 * inherit from Behaviour instead.
 */
class Component
{
public:
    Component()
        : m_Owner(nullptr), m_TypeInfo(nullptr), m_RefsResolved(false) {}
    virtual ~Component() = default;

    // Prevent copying - components have unique identity and owner
    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;

    /**
     * @brief The class's type id (TypeId<T>()), 0 for an instance with no type info.
     */
    ComponentType GetType() const { return m_TypeInfo ? m_TypeInfo->typeId : 0; }

    /**
     * @brief The direct component base's type id, so GetComponent<Base>() finds subclasses.
     */
    ComponentType GetBaseType() const { return m_TypeInfo ? m_TypeInfo->baseTypeId : 0; }

    /**
     * @brief The class entry this instance runs through. nullptr only for an
     *        instance created outside the engine's paths (a bare `new`).
     */
    const ComponentTypeInfo* GetTypeInfo() const { return m_TypeInfo; }

    /**
     * @brief Attach the class entry. Called by AddComponent<T>, the factory and
     *        the scene loader; user code never needs it.
     */
    void SetTypeInfo(const ComponentTypeInfo* info) { m_TypeInfo = info; }

    // Type casting without RTTI (for -fno-rtti builds)
    virtual Behaviour* AsBehaviour() { return nullptr; }

    /**
     * @brief Does this class (or a reflected base) declare Update()?
     *
     * Answered by the reflection codegen from the class declaration.
     * LifecycleManager only calls Update() on behaviours that answer true, so a
     * behaviour with no Update costs nothing per frame.
     */
    bool DeclaresUpdate() const { return m_TypeInfo && m_TypeInfo->declaresUpdate; }

    // Owner access
    Object* GetOwner() const { return m_Owner; }
    void SetOwner(Object* owner) { m_Owner = owner; }

    // ========== Lifecycle methods for Play Mode ==========
    // Components override these to handle asset loading and runtime updates
    // without the editor needing to know about specific component types.

    /**
     * @brief Load assets needed by this component
     *
     * Components override this to load their assets (sprites, fonts, etc.)
     * Called by Play Mode during initialization.
     * Use Deki::AssetManager::Get() to access the asset manager.
     */
    virtual void LoadAssets() {}

    /**
     * @brief Unload/clear assets held by this component
     *
     * Components override this to clear asset pointers before shutdown.
     * The asset manager owns the memory, so just set pointers to nullptr.
     */
    virtual void UnloadAssets() {}

    /**
     * @brief Update this component (runtime only)
     * @param deltaTime Time since last frame in seconds
     *
     * Components that need per-frame updates (physics, animation, etc.)
     * override this. Different from Behaviour::Update() which is for scripts.
     */
    virtual void RuntimeUpdate(float deltaTime) {}

    /**
     * @brief Called after an AssetRef property is resolved by LifecycleManager
     * @param propertyName Name of the property that was resolved
     * @param asset Pointer to the loaded asset
     * @param guid The GUID that was used to load the asset
     *
     * Components can override this to perform post-load processing —
     * e.g. inspecting whether the GUID resolves to a sub-asset and
     * deriving extra runtime state from it.
     */
    virtual void OnAssetRefResolved(const char* propertyName, void* asset, const char* guid)
    {
        (void)propertyName;
        (void)asset;
        (void)guid;
    }

    /**
     * @brief Check if this component needs asset loading
     * @return true if LoadAssets() should be called
     */
    virtual bool NeedsAssetLoading() const { return false; }

    /**
     * @brief Check if this component needs runtime updates
     * @return true if RuntimeUpdate() should be called each frame
     */
    virtual bool NeedsRuntimeUpdate() const { return false; }

    /**
     * @brief Deserialize component data from MessagePack
     * @param parser The msgpack parser positioned at component data map
     * @param mapSize Number of key-value pairs in the data map
     * @return true if deserialization succeeded
     *
     * Runs the codegen's hash-switch deserializer for this class. An instance
     * without type info skips the map so the parser stays in step.
     */
    bool Deserialize(SceneFormat::SceneMsgPackParser& parser, uint32_t mapSize);

    /**
     * @brief Resolve all ObjectRef properties in this component
     * @param scene The scene to look up objects in
     *
     * Called by LifecycleManager before Awake() to resolve ObjectRef.id → ObjectRef.resolved.
     * Zero-overhead: direct field access, no metadata iteration.
     */
    void ResolveObjectRefs(Scene* scene)
    {
        if (m_TypeInfo && m_TypeInfo->resolveObjectRefs) m_TypeInfo->resolveObjectRefs(this, scene);
    }

    /**
     * @brief Remap ObjectRef IDs after cloning/instantiation
     * @param idRemap Map of old object ID → new object ID
     *
     * Called after Instantiate() to update ObjectRef.id fields to new unique IDs.
     */
    void RemapObjectRefs(const std::unordered_map<uint32_t, uint32_t>& idRemap)
    {
        if (m_TypeInfo && m_TypeInfo->remapObjectRefs) m_TypeInfo->remapObjectRefs(this, idRemap);
    }

    // Ref resolution tracking (prevents per-frame re-resolution in edit mode)
    bool HasRefsResolved() const { return m_RefsResolved; }
    void MarkRefsResolved() { m_RefsResolved = true; }
    void ResetRefsResolved() { m_RefsResolved = false; }

    /**
     * @brief Called when a property is modified via the editor
     * @param propertyName Name of the property that changed
     *
     * Components can override this to react to property changes.
     * Called after Execute() and Undo() in ModifyComponentPropertyCommand.
     */
    virtual void OnPropertyChanged(const char* propertyName)
    {
        (void)propertyName;
    }

protected:
    Object* m_Owner;
    const ComponentTypeInfo* m_TypeInfo;
    bool m_RefsResolved;
};

}  // namespace Deki
