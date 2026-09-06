#include <deki/SceneSystem.h>

#include <deki/LogSystem.h>
#include <deki/Object.h>
#include <deki/Time.h>
#include <deki/Scene.h>
#include <deki/Engine.h>
#include "LifecycleManager.h"
#include <deki/assets/AssetManager.h>
#include <deki/providers/IRenderSystem.h>
#include <deki/providers/IDisplay.h>

namespace Deki
{


SceneSystem::SceneSystem()
    : m_RootScene(nullptr), m_OwnsRootScene(false) {}

SceneSystem::~SceneSystem()
{
    // Flush any deferred deletions first
    FlushPendingDeletions();

    // Clean up scene only if we own it
    if (m_RootScene && m_OwnsRootScene)
    {
        delete m_RootScene;
    }
    m_RootScene = nullptr;
    m_OwnsRootScene = false;

    // Clean up Persistent objects
    for (Object* obj : persistentObjects)
    {
        delete obj;
    }
    persistentObjects.clear();
}

bool SceneSystem::Setup()
{
    // Create a default scene
    m_RootScene = CreateScene();
    return m_RootScene != nullptr;
}

void SceneSystem::Update(uint32_t deltaTime)
{
    // Intentionally empty: behaviour lifecycle runs in LifecycleManager.
    (void)deltaTime;
}

void SceneSystem::SetRootScene(Scene* scene, bool takeOwnership)
{
    DEKI_LOG_INTERNAL("SceneSystem::SetRootScene: scene=%p, objects=%zu, takeOwnership=%d",
                      (void*)scene, scene ? scene->GetObjects().size() : 0, takeOwnership);

    // Queue old scene for deferred deletion (instead of immediate delete).
    // This prevents use-after-free when SetRootScene is called during
    // Start()/Update() while code is still executing on the old scene.
    if (m_RootScene && m_RootScene != scene && m_OwnsRootScene)
    {
        DEKI_LOG_INTERNAL("SceneSystem::SetRootScene: Queuing old scene %p for deferred deletion",
                          (void*)m_RootScene);
        m_PendingDeletion.push_back(m_RootScene);
    }

    m_RootScene = scene;
    m_OwnsRootScene = takeOwnership;

    // If taking ownership, release AssetManager's tracking to prevent double-free.
    // SceneSystem will handle deletion via deferred deletion queue.
    if (scene && takeOwnership)
    {
        auto* assetMgr = Deki::AssetManager::Get();
        if (assetMgr)
            assetMgr->ReleaseAssetTracking("Scene", scene);
    }
}

Scene* SceneSystem::CreateScene()
{
    Scene* scene = new Scene();
    return scene;
}

bool SceneSystem::LoadBootScene(const char* filename, Storage::Location storage)
{
    std::string fullPath = Storage::BuildPath(storage, filename);
    return LoadBootScene(fullPath.c_str());
}

bool SceneSystem::LoadBootScene(const char* path)
{
    if (!path)
    {
        DEKI_LOG_ERROR("SceneSystem::LoadBootScene: Path is null");
        return false;
    }

    DEKI_LOG_INTERNAL("SceneSystem: Loading boot scene '%s'", path);

    Scene* scene = Scene::LoadInternal(path);
    if (!scene)
    {
        DEKI_LOG_ERROR("SceneSystem::LoadBootScene: Failed to load '%s'", path);
        return false;
    }

    SetRootScene(scene);

    // Force immediate first frame render so boot screen shows instantly
    // Only run InternalAwake + Awake, then Render + Present
    // Start() happens AFTER the first render (in RunMainLoop)
    Engine& engine = Engine::GetInstance();
    LifecycleManager* lifecycle = engine.GetLifecycleManager();
    if (lifecycle)
    {
        lifecycle->ProcessInternalAwake(scene);
        lifecycle->ProcessAwake(scene);
    }
    engine.Render();

    // Present to display so it actually shows on screen (the first frame of a
    // fresh render system is always a full present).
    engine.PresentFrame();

    // Reset frame time so first Update() doesn't get huge deltaTime from loading
    Time::ResetFrameTime();

    DEKI_LOG_INTERNAL("SceneSystem: Boot scene loaded successfully");
    return true;
}

void SceneSystem::MarkPersistent(Object* obj)
{
    if (!obj)
        return;

    // Already marked?
    if (obj->IsPersistent())
    {
        // Check if already in our list
        for (Object* existing : persistentObjects)
        {
            if (existing == obj)
                return;  // Already tracked
        }
    }

    // Mark the flag
    obj->SetPersistent(true);

    // Detach from parent-child hierarchy to prevent cascade deletion
    obj->RemoveFromParent();

    // Remove from its current scene (if any)
    Scene* ownerScene = obj->GetOwnerScene();
    if (ownerScene)
    {
        ownerScene->RemoveObject(obj);
        obj->SetOwnerScene(nullptr);
        DEKI_LOG_INTERNAL("SceneSystem: Moved '%s' to Persistent storage", obj->GetName().c_str());
    }

    // Add to persistent storage
    persistentObjects.push_back(obj);
}

void SceneSystem::ClearPersistentObjects(bool deleteObjects)
{
    if (deleteObjects)
    {
        for (Object* obj : persistentObjects)
        {
            delete obj;
        }
    }
    persistentObjects.clear();
    DEKI_LOG_INTERNAL("SceneSystem: Cleared Persistent objects");
}

void SceneSystem::FlushPendingDeletions()
{
    if (m_PendingDeletion.empty()) return;

    DEKI_LOG_INTERNAL("SceneSystem: Flushing %zu pending scene deletions",
                      m_PendingDeletion.size());
    for (Scene* p : m_PendingDeletion)
    {
        delete p;
    }
    m_PendingDeletion.clear();
}

}  // namespace Deki
