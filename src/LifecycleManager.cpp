#include "LifecycleManager.h"

#include <deki/Scene.h>
#include <deki/Object.h>
#include <deki/Engine.h>
#include <deki/SceneSystem.h>
#include <deki/reflection/Property.h>
#include <deki/reflection/ObjectRef.h>
#include <deki/reflection/ComponentRegistry.h>
#include <deki/assets/AssetRef.h>
#include <deki/assets/AssetManager.h>
#include <deki/LogSystem.h>
#include <deki/profiling/Profiler.h>

#include <unordered_set>
#include <cstdio>
#include <vector>

// Helper to collect the components of Persistent objects

namespace Deki
{

static void CollectPersistentComponents(std::vector<Component*>& components)
{
    const auto& persistentObjects = Engine::GetInstance().GetSceneSystem().GetPersistentObjects();
    for (Object* obj : persistentObjects)
    {
        for (Component* comp : obj->GetComponents())
        {
            components.push_back(comp);
        }
    }
}

namespace
{
// Borrows a reusable scratch vector, falling back to its own storage if that
// buffer is already in use further up the stack. The snapshot semantics are
// unchanged either way - this only decides where the copy lives.
class ScratchSnapshot
{
public:
    ScratchSnapshot(std::vector<Component*>& shared, bool& inUse)
        : m_InUse(inUse),
          m_Owned(!inUse)
    {
        if (m_Owned)
        {
            inUse = true;
            m_Target = &shared;
            m_Target->clear();
        }
        else
        {
            m_Target = &m_Fallback;
        }
    }
    ~ScratchSnapshot()
    {
        if (m_Owned)
        {
            m_InUse = false;
        }
    }

    std::vector<Component*>& Get() { return *m_Target; }

private:
    bool& m_InUse;
    bool m_Owned;
    std::vector<Component*>* m_Target;
    std::vector<Component*> m_Fallback;
};
}  // namespace

// Static profiling callbacks - null by default (no profiling on embedded devices)
LifecycleProfileBeginCallback LifecycleManager::s_ProfileBegin = nullptr;
LifecycleProfileEndCallback LifecycleManager::s_ProfileEnd = nullptr;

void LifecycleManager::SetProfilingCallbacks(LifecycleProfileBeginCallback begin, LifecycleProfileEndCallback end)
{
    s_ProfileBegin = begin;
    s_ProfileEnd = end;
}

// Helper to get profile name for a component method
static void GetProfileName(char* buffer, size_t size, Component* component, const char* methodName)
{
    const ComponentMeta* meta = ComponentRegistry::Instance().GetMeta(component->GetType());
    if (meta && meta->name)
    {
        snprintf(buffer, size, "%s.%s", meta->name, methodName);
    }
    else
    {
        snprintf(buffer, size, "Unknown.%s", methodName);
    }
}

void LifecycleManager::ProcessInternalAwake(Scene* scene)
{
    DEKI_PROFILE_SCOPE_N("ProcessInternalAwake");
    if (!scene)
    {
        return;
    }

    char profileName[128];

    // Resolve all AssetRefs and ObjectRefs for components that haven't resolved yet
    // This happens BEFORE any user code (Awake/Start) runs
    // m_RefsResolved prevents per-frame re-resolution in edit mode (where HasAwoken is never set)
    // Snapshot the list: GetActiveComponents() returns the live, lazily-rebuilt cache, and
    // user code below can instantiate/destroy objects that rebuild it mid-iteration
    // (dangling pointers / UB). Iterate a copy; new components are handled on a later pass.
    ScratchSnapshot scratch(m_SnapshotScratch, m_SnapshotScratchInUse);
    std::vector<Component*>& snapshot = scratch.Get();
    snapshot.assign(scene->GetActiveComponents().begin(), scene->GetActiveComponents().end());
    for (auto* component : snapshot)
    {
        Object* owner = component->GetOwner();
        if (owner && owner->IsActiveInHierarchy() && !component->HasAwoken() && !component->HasRefsResolved())
        {
            // Profile with component name: "TestComponent.InternalAwake"
            if (s_ProfileBegin)
            {
                GetProfileName(profileName, sizeof(profileName), component, "InternalAwake");
                s_ProfileBegin(profileName);
            }

            LogSystem::SetLogContext(owner->GetId());
            ResolveAssetRefs(component);
            ResolveObjectRefs(component, scene);
            LogSystem::ClearLogContext();
            component->MarkRefsResolved();

            if (s_ProfileEnd)
            {
                s_ProfileEnd();
            }
        }
    }
}

void LifecycleManager::ProcessAwake(Scene* scene)
{
    DEKI_PROFILE_SCOPE_N("ProcessAwake");
    if (!scene)
    {
        return;
    }

    char profileName[128];

    // Snapshot (see ProcessInternalAwake): Awake() may instantiate/destroy objects and
    // rebuild the live components cache, which would invalidate a range-for over it.
    ScratchSnapshot scratch(m_SnapshotScratch, m_SnapshotScratchInUse);
    std::vector<Component*>& snapshot = scratch.Get();
    snapshot.assign(scene->GetActiveComponents().begin(), scene->GetActiveComponents().end());
    for (auto* component : snapshot)
    {
        Object* owner = component->GetOwner();
        if (owner && owner->IsActiveInHierarchy() && !component->HasAwoken())
        {
            // Profile with component name: "TestComponent.Awake"
            if (s_ProfileBegin)
            {
                GetProfileName(profileName, sizeof(profileName), component, "Awake");
                s_ProfileBegin(profileName);
            }

            LogSystem::SetLogContext(owner->GetId());
            component->Awake();
            LogSystem::ClearLogContext();
            component->MarkAwoken();

            if (s_ProfileEnd)
            {
                s_ProfileEnd();
            }
        }
    }
}

void LifecycleManager::ProcessStart(Scene* scene)
{
    DEKI_PROFILE_SCOPE_N("ProcessStart");
    if (!scene)
    {
        return;
    }

    char profileName[128];

    // Snapshot (see ProcessInternalAwake): Start() may instantiate/destroy objects and
    // rebuild the live components cache, which would invalidate a range-for over it.
    ScratchSnapshot scratch(m_SnapshotScratch, m_SnapshotScratchInUse);
    std::vector<Component*>& snapshot = scratch.Get();
    snapshot.assign(scene->GetActiveComponents().begin(), scene->GetActiveComponents().end());
    for (auto* component : snapshot)
    {
        Object* owner = component->GetOwner();
        if (owner && owner->IsActiveInHierarchy() && !component->HasStarted())
        {
            // Profile with component name: "TestComponent.Start"
            if (s_ProfileBegin)
            {
                GetProfileName(profileName, sizeof(profileName), component, "Start");
                s_ProfileBegin(profileName);
            }

            LogSystem::SetLogContext(owner->GetId());
            component->Start();
            LogSystem::ClearLogContext();
            component->MarkStarted();

            if (s_ProfileEnd)
            {
                s_ProfileEnd();
            }
        }
    }
}

void LifecycleManager::ProcessUpdate(Scene* scene)
{
    DEKI_PROFILE_SCOPE_N("ProcessUpdate");
    if (!scene)
    {
        return;
    }

    char profileName[128];

    // Collect all components: scene + Persistent objects
    ScratchSnapshot scratch(m_UpdateScratch, m_UpdateScratchInUse);
    std::vector<Component*>& allComponents = scratch.Get();
    const auto& live = scene->GetActiveComponents();
    // assign() rather than a push_back loop: the loop reallocated as it grew,
    // which is why the allocation count climbed with component count.
    allComponents.assign(live.begin(), live.end());
    CollectPersistentComponents(allComponents);

    for (auto* component : allComponents)
    {
        // DeclaresUpdate() comes from the class declaration: no Update member,
        // no call. A data component is skipped here at the cost of one load.
        if (!component->DeclaresUpdate())
        {
            continue;
        }
        Object* owner = component->GetOwner();
        if (owner && owner->IsActiveInHierarchy() && component->HasStarted())
        {
            // Profile with component name: "TestComponent.Update"
            if (s_ProfileBegin)
            {
                GetProfileName(profileName, sizeof(profileName), component, "Update");
                s_ProfileBegin(profileName);
            }

            LogSystem::SetLogContext(owner->GetId());
            component->Update();
            LogSystem::ClearLogContext();

            if (s_ProfileEnd)
            {
                s_ProfileEnd();
            }
        }
    }
}

// Recursively drive RuntimeUpdate() on every opted-in component in the subtree.
// Mirrors the editor's updateComponentsRecursive (PlayMode.cpp) so the standalone
// runtime ticks component physics identically to play-in-editor.
static void RuntimeUpdateRecursive(Object* obj, float deltaTime)
{
    if (!obj)
    {
        return;
    }

    for (Component* comp : obj->GetComponents())
    {
        if (comp->NeedsRuntimeUpdate())
        {
            if (LifecycleManager::s_ProfileBegin)
            {
                const ComponentMeta* meta = ComponentRegistry::Instance().GetMeta(comp->GetType());
                LifecycleManager::s_ProfileBegin(meta && meta->name ? meta->name : "Unknown");
            }

            comp->RuntimeUpdate(deltaTime);

            if (LifecycleManager::s_ProfileEnd)
            {
                LifecycleManager::s_ProfileEnd();
            }
        }
    }

    for (Object* child : obj->GetChildren())
    {
        RuntimeUpdateRecursive(child, deltaTime);
    }
}

void LifecycleManager::ProcessRuntimeUpdate(Scene* scene, float deltaTime)
{
    DEKI_PROFILE_SCOPE_N("ProcessRuntimeUpdate");
    if (!scene)
    {
        return;
    }

    for (Object* obj : scene->GetObjects())
    {
        RuntimeUpdateRecursive(obj, deltaTime);
    }
}

void LifecycleManager::ResolveObjectRefs(Component* component, Scene* scene)
{
    // Zero-overhead: call generated virtual method that does direct field access
    component->ResolveObjectRefs(scene);
}

void LifecycleManager::ResolveAssetRefs(Component* component)
{
    // Fast path: skip types known to have no metadata (avoids repeated hash lookups)
    static std::unordered_set<uint32_t> s_NoMetaTypes;

    uint32_t typeId = component->GetType();
    if (s_NoMetaTypes.count(typeId))
    {
        return;
    }

    // Get component metadata
    const ComponentMeta* meta = ComponentRegistry::Instance().GetMeta(typeId);
    if (!meta || !meta->properties)
    {
        s_NoMetaTypes.insert(typeId);
        return;
    }

    auto* assetMgr = Deki::AssetManager::Get();
    if (!assetMgr)
    {
        return;
    }

    // Iterate through properties looking for AssetRef types
    for (int i = 0; i < meta->propertyCount; ++i)
    {
        const PropertyInfo& prop = meta->properties[i];
        if (prop.type != PropertyType::AssetRef)
        {
            continue;
        }
        if (!prop.assetType)
        {
            continue;
        }

        // Skip non-cacheable asset types (e.g., Scene) during eager resolution.
        // These are loaded on-demand when the component calls AssetRef::Get().
        if (!assetMgr->IsTypeCacheable(prop.assetType))
        {
            continue;
        }

        // Get the AssetRefBase at the property offset
        Deki::AssetRefBase* ref =
            reinterpret_cast<Deki::AssetRefBase*>(reinterpret_cast<char*>(component) + prop.offset);

        // Skip if already loaded, no GUID, or already attempted
        if (ref->ptr || ref->guid.empty() || ref->loadAttempted)
        {
            continue;
        }

        // Mark as attempted before trying
        ref->loadAttempted = true;

        DEKI_LOG_INTERNAL("ResolveAssetRefs: Loading '%s' GUID=%s type=%s", prop.name, ref->guid.c_str(),
                          prop.assetType);

        // Load the asset using unified AssetManager
        ref->ptr = assetMgr->LoadByGuidAndType(ref->guid, prop.assetType);

        // Notify component of resolved asset (for SubAsset frame lookup, etc.)
        if (ref->ptr)
        {
            // For sub-assets, source holds the frame GUID needed for FindFrame()
            const char* resolveGuid = ref->source.empty() ? ref->guid.c_str() : ref->source.c_str();
            component->OnAssetRefResolved(prop.name, ref->ptr, resolveGuid);
        }
        else
        {
            // Allow AssetRef::Get() to retry later (e.g., editor fonts compiled on-demand)
            ref->loadAttempted = false;
            DEKI_LOG_WARNING("AssetRef '%s' failed to load: GUID=%s, type=%s", prop.name, ref->guid.c_str(),
                             prop.assetType);
        }
    }
}

}  // namespace Deki
