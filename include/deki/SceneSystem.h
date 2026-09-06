#pragma once

#include <stdint.h>
#include <vector>
#include <deki/Storage.h>

// Forward declarations

namespace Deki
{

class Scene;
class Object;

/**
 * @brief SceneSystem class for managing the root scene
 *
 * This class handles scene loading, management, and lifecycle operations.
 * It provides a clean separation between the Engine core and scene management.
 *
 * The "root scene" is the currently running scene.
 */
class SceneSystem
{
private:
    // Current root scene
    Scene* m_RootScene;

    // Whether SceneSystem owns (and should delete) the root scene
    bool m_OwnsRootScene;

    // Objects marked as Persistent - persisted independently of any scene
    // These are never deleted when scenes change
    std::vector<Object*> persistentObjects;

    // Deferred deletion queue: old scenes queued for deletion during SetRootScene.
    // Flushed at the start of the next frame to avoid use-after-free when
    // SetRootScene is called during Start()/Update().
    std::vector<Scene*> m_PendingDeletion;

public:
    /**
     * @brief Constructor - initializes the scene system
     */
    SceneSystem();

    /**
     * @brief Destructor - cleans up resources
     */
    ~SceneSystem();

    /**
     * @brief Setup the scene system
     * @return true if setup successful, false otherwise
     */
    bool Setup();

    /**
     * @brief Update the current scene
     * @param deltaTime Time since last update in milliseconds
     */
    void Update(uint32_t deltaTime);

    /**
     * @brief Set the current root scene
     * @param scene Pointer to the scene to set as root
     * @param takeOwnership If true, SceneSystem will delete the scene when replaced or on shutdown
     */
    void SetRootScene(Scene* scene, bool takeOwnership = true);

    /**
     * @brief Create a new empty scene
     * @return Pointer to the created scene
     */
    Scene* CreateScene();

    /**
     * @brief Load a boot scene from file and set it as active
     * @param path Full path to the scene file (including storage prefix)
     * @return true if loaded successfully
     */
    bool LoadBootScene(const char* path);

    /**
     * @brief Load a boot scene from a storage location
     * @param filename Scene filename (e.g., "boot.scene")
     * @param storage Storage location to load from
     * @return true if loaded successfully
     */
    bool LoadBootScene(const char* filename, Storage::Location storage);

    /**
     * @brief Get the current root scene
     * @return Pointer to the current scene
     */
    Scene* GetRootScene() const { return m_RootScene; }

    /**
     * @brief Check if there is a root scene
     * @return true if there is a root scene, false otherwise
     */
    bool HasRootScene() const { return m_RootScene != nullptr; }

    /**
     * @brief Mark an object as Persistent and move it to persistent storage
     * The object will be removed from its current scene and persisted independently.
     * It will survive scene changes without being destroyed or re-added.
     * @param obj The object to persist
     */
    void MarkPersistent(Object* obj);

    /**
     * @brief Get all Persistent objects
     * @return Reference to the vector of persistent objects
     */
    const std::vector<Object*>& GetPersistentObjects() const { return persistentObjects; }

    /**
     * @brief Clear all Persistent objects (call when exiting play mode in editor)
     * @param deleteObjects If true, deletes the objects. If false, just clears the list.
     */
    void ClearPersistentObjects(bool deleteObjects = true);

    /**
     * @brief Flush any scenes queued for deferred deletion.
     *
     * Called at the START of ProcessLifecycle / play mode tick, before any
     * behaviour code runs. Safe because no code is iterating over the old scene.
     */
    void FlushPendingDeletions();
};

}  // namespace Deki
