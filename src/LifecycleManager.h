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
class Behaviour;

// Profiling callback types - set by editor, null on embedded devices
typedef void (*LifecycleProfileBeginCallback)(const char* name);
typedef void (*LifecycleProfileEndCallback)();

/**
 * @brief Manages Behaviour lifecycle (InternalAwake, Awake, Start, Update)
 *
 * Centralizes all lifecycle method invocation and reference resolution.
 * Called by Engine during the update phase, before rendering.
 *
 * Lifecycle order:
 * 1. ProcessInternalAwake() - Resolves AssetRefs and ObjectRefs
 * 2. ProcessAwake() - Calls Awake() on all behaviours
 * 3. ProcessStart() - Calls Start() on all behaviours
 * 4. ProcessUpdate() - Called every frame for started behaviours
 * 5. Render (handled separately by render system)
 */
class LIFECYCLE_MANAGER_API LifecycleManager
{
public:
    LifecycleManager() = default;
    ~LifecycleManager() = default;

    // Profiling callbacks - set by editor for Unity-style per-component profiling
    static LifecycleProfileBeginCallback s_ProfileBegin;
    static LifecycleProfileEndCallback s_ProfileEnd;

    /**
     * @brief Set profiling callbacks for lifecycle method instrumentation
     *
     * When set, these callbacks are invoked around each Update/Start/Awake call
     * with the component type name (e.g., "TestComponent.Update").
     * Set to nullptr to disable profiling (default on embedded devices).
     */
    static void SetProfilingCallbacks(LifecycleProfileBeginCallback begin, LifecycleProfileEndCallback end);

    /**
     * @brief Resolve AssetRefs and ObjectRefs for all behaviours that haven't awoken
     *
     * This is the internal setup phase where the engine resolves all
     * dependencies before any user code runs.
     * @param scene The current scene
     */
    void ProcessInternalAwake(Scene* scene);

    /**
     * @brief Process Awake for all behaviours that haven't awoken yet
     *
     * Called after InternalAwake. All asset and object references are
     * available at this point.
     * @param scene The current scene
     */
    void ProcessAwake(Scene* scene);

    /**
     * @brief Process Start for all behaviours that haven't started yet
     *
     * Called after all Awake calls complete. Safe to access other components.
     * @param scene The current scene
     */
    void ProcessStart(Scene* scene);

    /**
     * @brief Process Update for all behaviours that have started
     * @param scene The current scene
     */
    void ProcessUpdate(Scene* scene);

    /**
     * @brief Drive RuntimeUpdate() on every component that opts in via
     *        NeedsRuntimeUpdate()
     *
     * This is the engine-runtime counterpart to the editor play-mode walk
     * (EditorApp::UpdatePlayMode -> updateComponentsRecursive). Components such
     * as RollerComponent / ScrollComponent put their per-frame physics in
     * RuntimeUpdate(), which is distinct from the script Behaviour::Update()
     * driven by ProcessUpdate(). Without this call their momentum/snap state
     * machines never tick outside the editor.
     *
     * @param scene The current scene
     * @param deltaTime Time since last frame in SECONDS
     */
    void ProcessRuntimeUpdate(Scene* scene, float deltaTime);

private:
    /**
     * Scratch buffers for the per-phase behaviour snapshots.
     *
     * Each phase has to iterate a *copy* of the scene's behaviour list, because
     * user code can instantiate or destroy objects and rebuild the live cache
     * mid-iteration. Building that copy as a fresh local vector allocated on
     * every phase of every frame: measured at 4 allocations/frame with a single
     * behaviour, rising to 12/frame and ~8.9 KB of churn at 200. On an MCU that
     * is a steady fragmentation source and per-frame jitter, for buffers whose
     * contents are discarded immediately.
     *
     * Reusing these keeps the copy (and its safety) while dropping the
     * allocation: clear() preserves capacity, so steady state is allocation-free
     * once the high-water mark is reached.
     *
     * Not re-entrant. ReentrancyGuard in the .cpp falls back to a local vector
     * if a phase is somehow entered from inside itself, so behaviour stays
     * correct even if that ever happens.
     */
    std::vector<Behaviour*> m_SnapshotScratch;
    std::vector<Behaviour*> m_UpdateScratch;
    bool m_SnapshotScratchInUse = false;
    bool m_UpdateScratchInUse = false;

    /**
     * @brief Resolve ObjectRef properties on a component
     * @param component The component to resolve
     * @param scene The scene to look up objects in
     */
    void ResolveObjectRefs(Component* component, Scene* scene);

    /**
     * @brief Resolve AssetRef properties on a component
     *
     * Scans component metadata for AssetRef properties and loads assets
     * using AssetManager::LoadByGuidAndType().
     * @param component The component to resolve
     */
    void ResolveAssetRefs(Component* component);
};

}  // namespace Deki
