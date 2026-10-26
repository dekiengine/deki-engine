#pragma once

#include <stdint.h>
#include <vector>
#include <deki/Storage.h>

namespace Deki
{

class Scene;
class Object;

/// Loads and owns the root scene, the scene that is running, and keeps the
/// persistent objects that outlive scene changes.
class SceneSystem
{
private:
    Scene* m_RootScene;

    // Whether to delete the root scene when it is replaced or on shutdown.
    bool m_OwnsRootScene;

    // Objects marked Persistent. They belong to no scene and are never
    // deleted when scenes change.
    std::vector<Object*> m_PersistentObjects;

    // Scenes replaced by SetRootScene, deleted at the start of the next
    // frame: SetRootScene may be called from Start()/Update() while the old
    // scene is still being iterated.
    std::vector<Scene*> m_PendingDeletion;

    // Counts the root scenes installed (GetRootSceneSerial).
    uint32_t m_RootSceneSerial = 0;

public:
    SceneSystem();

    ~SceneSystem();

    /// Returns false on failure.
    bool Setup();

    /// Updates the current scene. `deltaTime` is in milliseconds.
    void Update(uint32_t deltaTime);

    /// Makes `scene` the root scene. With `takeOwnership`, it is deleted when
    /// replaced or on shutdown.
    void SetRootScene(Scene* scene, bool takeOwnership = true);

    /// Creates an empty scene.
    Scene* CreateScene();

    /// Loads the scene at `path` (full path with storage prefix) and makes it
    /// the root scene. Returns false on failure.
    bool LoadBootScene(const char* path);

    /// Loads `filename` (such as "boot.scene") from `storage` and makes it the
    /// root scene. Returns false on failure.
    bool LoadBootScene(const char* filename, Storage::Location storage);

    Scene* GetRootScene() const { return m_RootScene; }

    /// Changes every time the root scene is replaced. Compare it, not the
    /// scene's address, to tell whether the scene changed: a scene switch
    /// frees the old scene before loading the next, which may then be
    /// allocated at the very same address.
    uint32_t GetRootSceneSerial() const { return m_RootSceneSerial; }

    bool HasRootScene() const { return m_RootScene != nullptr; }

    /// Removes `obj` from its scene and keeps it alive across scene changes.
    void MarkPersistent(Object* obj);

    const std::vector<Object*>& GetPersistentObjects() const { return m_PersistentObjects; }

    /// Empties the persistent list, deleting the objects when `deleteObjects`.
    /// The editor calls it when play mode ends.
    void ClearPersistentObjects(bool deleteObjects = true);

    /// Deletes the scenes queued by SetRootScene. Called at the start of
    /// ProcessLifecycle and of the play mode tick, before any behaviour code
    /// runs, and before a scene set later in the frame is started: when
    /// nothing iterates the old scene.
    void FlushPendingDeletions();
};

}  // namespace Deki
