#pragma once

// DLL export for editor builds
#ifdef DEKI_EDITOR
#ifdef _WIN32
#ifdef DEKI_ENGINE_EXPORTS
#define LIFECYCLE_MANAGER_API __declspec(dllexport)
#else
#define LIFECYCLE_MANAGER_API __declspec(dllimport)
#endif
#else
#define LIFECYCLE_MANAGER_API
#endif
#else
#define LIFECYCLE_MANAGER_API
#endif

#include <vector>

// Forward declarations

namespace Deki
{

class Scene;
class Component;

// Profiling callback types - set by editor, null on embedded devices
typedef void (*LifecycleProfileBeginCallback)(const char* name);
typedef void (*LifecycleProfileEndCallback)();

/// Runs the component lifecycle (InternalAwake, Awake, Start, Update) and
/// resolves references. Engine calls it in the update phase, before rendering.
///
/// Lifecycle order:
/// 1. ProcessInternalAwake() - resolves AssetRefs and ObjectRefs
/// 2. ProcessAwake() - calls Awake() on every component that has not awoken
/// 3. ProcessStart() - calls Start() on every component that has not started
/// 4. ProcessUpdate() - every frame, on started components whose class declares Update()
/// 5. Render (done by the render system)
class LIFECYCLE_MANAGER_API LifecycleManager
{
public:
    LifecycleManager() = default;
    ~LifecycleManager() = default;

    // Profiling callbacks - set by editor for Unity-style per-component profiling
    static LifecycleProfileBeginCallback s_ProfileBegin;
    static LifecycleProfileEndCallback s_ProfileEnd;

    /// Sets callbacks run around each Update/Start/Awake call, named after
    /// the component type and method (such as "TestComponent.Update").
    /// nullptr turns profiling off, as on embedded devices.
    static void SetProfilingCallbacks(LifecycleProfileBeginCallback begin, LifecycleProfileEndCallback end);

    /// Resolves AssetRefs and ObjectRefs for every behaviour in `scene` that
    /// has not awoken, before any user code runs.
    void ProcessInternalAwake(Scene* scene);

    /// Calls Awake on every behaviour in `scene` that has not awoken. Runs
    /// after InternalAwake, so all asset and object references are resolved.
    void ProcessAwake(Scene* scene);

    /// Calls Start on every behaviour in `scene` that has not started. Runs
    /// after every Awake, so other components are safe to use.
    void ProcessStart(Scene* scene);

    /// Calls Update on every started behaviour in `scene`.
    void ProcessUpdate(Scene* scene);

    /// Calls RuntimeUpdate() on every component that opts in through
    /// NeedsRuntimeUpdate(). `deltaTime` is in SECONDS.
    ///
    /// The engine-side counterpart of the editor play-mode walk
    /// (EditorApp::UpdatePlayMode -> updateComponentsRecursive). Components
    /// such as RollerComponent and ScrollComponent put their per-frame physics
    /// in RuntimeUpdate(), apart from the script-facing Component::Update()
    /// that ProcessUpdate() drives. Without this call their momentum and snap
    /// state machines never tick outside the editor.
    void ProcessRuntimeUpdate(Scene* scene, float deltaTime);

private:
    // Scratch buffers for the per-phase component snapshots.
    //
    // Each phase iterates a *copy* of the scene's component list, because user
    // code can create or destroy objects and rebuild the live cache
    // mid-iteration. A fresh vector per phase would allocate every frame (12
    // allocations and ~8.9 KB a frame with 200 behaviours), which on an MCU
    // fragments the heap and adds jitter. clear() keeps capacity, so once the
    // high-water mark is reached nothing is allocated.
    //
    // Not re-entrant: ReentrancyGuard in the .cpp falls back to a local vector
    // if a phase is entered from inside itself.
    std::vector<Component*> m_SnapshotScratch;
    std::vector<Component*> m_UpdateScratch;
    bool m_SnapshotScratchInUse = false;
    bool m_UpdateScratchInUse = false;

    // Resolves the component's ObjectRef properties against objects in `scene`.
    void ResolveObjectRefs(Component* component, Scene* scene);

    // Finds the component's AssetRef properties in its metadata and loads
    // them with AssetManager::LoadByGuidAndType().
    void ResolveAssetRefs(Component* component);
};

}  // namespace Deki
