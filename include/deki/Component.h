#pragma once

#include <cstdint>
#include <unordered_map>

#include <deki/TypeId.h>

namespace Deki
{

class Object;
class Component;
class Scene;
struct FieldRef;
struct ComponentMeta;
namespace SceneFormat
{
class SceneMsgPackParser;
}

/// Everything the engine knows about one component class.
///
/// The reflection codegen emits one per class into the owning package's
/// generated sources, and it is registered with ComponentFactory at startup.
/// Each instance points at its class's entry (Component::GetTypeInfo), which
/// is how GetType(), Deserialize() and the reference fixups work without the
/// class declaring anything itself.
///
/// All strings are literals with static storage.
struct ComponentTypeInfo
{
    const char* name;          // fully qualified C++ name: the on-disk key; HashString(name) == typeId
    const char* shortName;     // the identifier alone, for display
    ComponentType typeId;      // TypeId<T>()
    ComponentType baseTypeId;  // TypeId of the direct component base (Deki::Component or a user base)
    bool isAbstract;           // no create()
    bool declaresUpdate;       // the class (or a reflected base) declares Update(); see Component::DeclaresUpdate

    Component* (*create)();  // new instance with its type info set; nullptr when abstract
    bool (*deserialize)(Component* self, SceneFormat::SceneMsgPackParser& parser, uint32_t mapSize);
    void (*resolveObjectRefs)(Component* self, Scene* scene);
    void (*remapObjectRefs)(Component* self, const std::unordered_map<uint32_t, uint32_t>& idRemap);

    const FieldRef* fields;  // flattened field table (own + inherited), every platform
    int fieldCount;

    const ComponentMeta* meta;  // editor-only rich metadata; nullptr on device builds
};

/// The base class of every component.
///
/// A component is any class deriving publicly from Component; it declares
/// nothing else. The reflection codegen finds the class by its base, its
/// identity is its qualified name (see TypeId.h), and the per-class operations
/// below run through the ComponentTypeInfo the engine attaches when the
/// instance is created (AddComponent<T>, the scene loader, the factory).
///
/// A component with exported fields and no lifecycle methods is data. One that
/// declares Awake(), Start() or Update() takes part in the lifecycle. Only a
/// class that declares Update() is ticked, so a data component costs nothing
/// per frame.
///
/// Lifecycle order:
/// 1. InternalAwake (engine) - resolves AssetRefs and ObjectRefs
/// 2. Awake()  - once, after the references are resolved
/// 3. Start()  - once, before the first Update, after every Awake has run
/// 4. Update() - every frame
/// 5. Render (RendererComponent)
class Component
{
public:
    Component()
        : m_Owner(nullptr),
          m_TypeInfo(nullptr),
          m_RefsResolved(false),
          m_HasAwoken(false),
          m_HasStarted(false)
    {
    }
    virtual ~Component() = default;

    // Not copyable: a component has one identity and one owner.
    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;

    /// The class's type id (TypeId<T>()), 0 for an instance with no type info.
    ComponentType GetType() const { return m_TypeInfo ? m_TypeInfo->typeId : 0; }

    /// The direct component base's type id, so GetComponent<Base>() finds subclasses.
    ComponentType GetBaseType() const { return m_TypeInfo ? m_TypeInfo->baseTypeId : 0; }

    /// The class entry this instance runs through. nullptr only for an
    /// instance created outside the engine's paths (a bare `new`).
    const ComponentTypeInfo* GetTypeInfo() const { return m_TypeInfo; }

    /// Attaches the class entry. AddComponent<T>, the factory and the scene
    /// loader call this; user code never needs it.
    void SetTypeInfo(const ComponentTypeInfo* info) { m_TypeInfo = info; }

    /// True when this class (or a reflected base) declares Update(), as the
    /// codegen read it from the class declaration. LifecycleManager calls
    /// Update() only on these.
    bool DeclaresUpdate() const { return m_TypeInfo && m_TypeInfo->declaresUpdate; }

    Object* GetOwner() const { return m_Owner; }
    void SetOwner(Object* owner) { m_Owner = owner; }

    // ========== Lifecycle (declare the ones you need) ==========

    /// Called once, after AssetRefs and ObjectRefs are resolved. Use it for
    /// self-initialisation.
    virtual void Awake() {}

    /// Called once before the first Update, after every component's Awake has
    /// run. Use it for setup that touches other components.
    virtual void Start() {}

    /// Called every frame, only on classes that declare it.
    virtual void Update() {}

    bool HasAwoken() const { return m_HasAwoken; }
    bool HasStarted() const { return m_HasStarted; }
    void MarkAwoken() { m_HasAwoken = true; }
    void MarkStarted() { m_HasStarted = true; }

    // ========== Lifecycle methods for Play Mode ==========
    // Components override these to load assets and run per-frame engine work,
    // so the editor needs no knowledge of specific component types.

    /// Loads the assets this component needs (sprites, fonts, ...). Play Mode
    /// calls it during initialisation. Use Deki::AssetManager::Get().
    virtual void LoadAssets() {}

    /// Drops this component's asset pointers before shutdown. The asset
    /// manager owns the memory, so only set the pointers to nullptr.
    virtual void UnloadAssets() {}

    /// Engine-driven per-frame work (physics, animation, ...), separate from
    /// the script-facing Update(): it runs in every mode, including the editor
    /// in edit mode, on components whose NeedsRuntimeUpdate() is true.
    /// `deltaTime` is in seconds.
    virtual void RuntimeUpdate(float deltaTime) {}

    /// Called after LifecycleManager resolves the AssetRef field `propertyName`
    /// to `asset`, loaded from `guid`. Override it for post-load work, such as
    /// checking whether the GUID names a sub-asset and deriving state from it.
    virtual void OnAssetRefResolved(const char* propertyName, void* asset, const char* guid)
    {
        (void)propertyName;
        (void)asset;
        (void)guid;
    }

    /// True when LoadAssets() should be called.
    virtual bool NeedsAssetLoading() const { return false; }

    /// True when RuntimeUpdate() should be called each frame.
    virtual bool NeedsRuntimeUpdate() const { return false; }

    /// Reads this component's fields from the MessagePack map the parser is
    /// at (`mapSize` key-value pairs), through the codegen's hash-switch
    /// deserializer. An instance without type info skips the map so the
    /// parser stays in step. Returns false on failure.
    bool Deserialize(SceneFormat::SceneMsgPackParser& parser, uint32_t mapSize);

    /// Points every ObjectRef field at its object in `scene` (ObjectRef.id ->
    /// ObjectRef.resolved). LifecycleManager calls it before Awake(). The
    /// generated code reads the fields directly, with no metadata walk.
    void ResolveObjectRefs(Scene* scene)
    {
        if (m_TypeInfo && m_TypeInfo->resolveObjectRefs)
        {
            m_TypeInfo->resolveObjectRefs(this, scene);
        }
    }

    /// Rewrites ObjectRef ids through `idRemap` (old id -> new id), after
    /// Instantiate() gave the copies new ids.
    void RemapObjectRefs(const std::unordered_map<uint32_t, uint32_t>& idRemap)
    {
        if (m_TypeInfo && m_TypeInfo->remapObjectRefs)
        {
            m_TypeInfo->remapObjectRefs(this, idRemap);
        }
    }

    // Tracks resolution so edit mode does not resolve references every frame.
    bool HasRefsResolved() const { return m_RefsResolved; }
    void MarkRefsResolved() { m_RefsResolved = true; }
    void ResetRefsResolved() { m_RefsResolved = false; }

    /// Called when the editor changes the field `propertyName`, after both
    /// Execute() and Undo() of ModifyComponentPropertyCommand.
    virtual void OnPropertyChanged(const char* propertyName) { (void)propertyName; }

protected:
    Object* m_Owner;
    const ComponentTypeInfo* m_TypeInfo;
    bool m_RefsResolved;
    bool m_HasAwoken;
    bool m_HasStarted;
};

}  // namespace Deki
