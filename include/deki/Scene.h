#pragma once
#include <algorithm>  // For std::sort
#include <cstdint>
#include <utility>
#include <vector>
#include <string>
#include <functional>

#include <deki/Object.h>
#include <deki/Component.h>
#include <deki/Export.h>  // DEKI_ENGINE_API - NOT Engine.h, which includes us back

#ifdef DEKI_EDITOR
#include <nlohmann/json_fwd.hpp>
#endif

// The namespace opens OUTSIDE the editor guard: it used to sit inside, so a
// runtime build (simulator, firmware; no DEKI_EDITOR) got Scene at global
// scope with an unbalanced brace and failed on the first `Object*`.
namespace Deki
{

#ifdef DEKI_EDITOR
struct ComponentMeta;  // reflection/Property.h; the docs-facing helpers take it by reference
struct PropertyInfo;
#endif

/**
 * @brief Represents a scene containing objects and components
 *
 * A Scene is the fundamental content unit in Deki Engine. It can contain
 * objects with components and can be:
 * - Loaded as the active/running scene
 * - Instanced inside another scene (what other engines call a prefab)
 * - Distributed and loaded at runtime
 *
 * This unified concept eliminates the scene/prefab distinction other engines
 * draw - here everything is a scene.
 */
class Scene
{
public:
    /// Asset type name for AssetManager::Load<T>() lookup
    static constexpr const char* AssetTypeName = "Scene";

    Scene();
    ~Scene();

    // =========================================================================
    // Static loading methods
    // =========================================================================

    /**
     * @brief Load a scene by asset path through AssetManager
     *
     * Always returns a fresh Scene instance with the original data.
     *
     * @param path Asset-relative path without extension (e.g., "scenes/settings/settings")
     * @param cache If true, pins the raw file bytes in RAM so subsequent loads skip disk I/O.
     *              If false (default), reads from disk every time. Either way, a fresh instance
     *              is returned - cached bytes are never mutated.
     * @return Loaded scene or nullptr on failure
     */
    static Scene* Load(const char* path, bool cache = false);

    /**
     * @brief Load a scene directly from the filesystem (binary MessagePack)
     * @param path Resolved filesystem path to the compiled .scene binary
     * @return Loaded scene or nullptr on failure
     * @note Used internally by AssetManager and SceneSystem. Prefer Load() for user code.
     */
    static Scene* LoadInternal(const char* path);

    /**
     * @brief Load a scene from memory buffer
     * @param data Pointer to scene data
     * @param size Size of the data
     * @return Loaded scene or nullptr on failure
     */
    static Scene* LoadFromMemory(const uint8_t* data, size_t size);

    // =========================================================================
    // Instantiation (for nesting scenes)
    // =========================================================================

    /**
     * @brief Instantiate this scene into another scene
     * @param targetScene The scene to add the instanced objects to
     * @return The root Object of the instanced scene, or nullptr on failure
     */
    Object* Instantiate(Scene* targetScene);

    /**
     * @brief Instantiate this scene with a specific position
     * @param targetScene The scene to add the instanced objects to
     * @param x X position for the root object
     * @param y Y position for the root object
     * @return The root Object of the instanced scene, or nullptr on failure
     */
    Object* Instantiate(Scene* targetScene, float x, float y);

    // =========================================================================
    // Object management
    // =========================================================================

    void AddObject(Object* obj);
    void RemoveObject(Object* obj);

    const std::vector<Object*>& GetObjects() const { return objects; }

    // Every component of every active object, for the per-frame lifecycle.
    // Rebuilt lazily when dirty.
    const std::vector<Component*>& GetActiveComponents();

    // Mark cache as needing rebuild (deferred until next GetActiveComponents() call)
    void InvalidateComponentsCache() { m_ComponentsCacheDirty = true; }

    // Object creation/destruction
    Object* CreateDekiObject(const std::string& name = "DekiObject");
    void DestroyDekiObject(Object* dekiObject);
    Object* FindDekiObject(const std::string& name);
    Object* FindDekiObject(uint32_t id);

    // Load scene from binary data (MessagePack format)
    bool LoadData(const uint8_t* data, size_t size);

#ifdef DEKI_EDITOR
    // Editor-only: Insert object at specific index (for reordering)
    void InsertObjectAt(Object* obj, size_t index);
    int GetObjectIndex(Object* obj) const;

    // Editor-only: Save/Load scene to/from file (JSON format)
    bool SaveToFile(const std::string& filePath);
    bool LoadFromFile(const std::string& filePath);

    // Editor-only: Serialize scene to JSON string (for play mode backup)
    std::string SaveToJsonString();

    // Editor-only: Load scene from JSON string (for hot reload)
    bool LoadFromJsonString(const std::string& jsonStr);

    // Object duplication (deep clone via JSON round-trip)
    Object* DuplicateObject(const Object* source);

    // Scene instance support
    static DEKI_ENGINE_API std::function<std::string(const std::string& guid)> s_GuidToPathResolver;
    bool ExpandInstance(Object* instanceObj);
    void CollapseInstance(Object* instanceObj);
    void ApplyOverrides(Object* instanceObj, const nlohmann::json& overrides);
    void CollectOverrides(const Object* instanceRoot, const Object* obj,
                          nlohmann::json& overrides) const;

    /// Property keys found in a scene file that no component's current metadata
    /// recognises, as {component type name, key}.
    ///
    /// Reflected member names ARE the scene-file format: reflection_codegen
    /// emits each C++ identifier as a HashString key, with no opt-in gate.
    /// So renaming a reflected member silently changes the on-disk key, the
    /// parser's switch falls through, and the property reverts to its default -
    /// no error, no log line. The value itself survives (unknown keys are
    /// re-emitted on save), but the behaviour is gone.
    ///
    /// This is the detector for that. Load every scene in a project and assert
    /// this comes back empty; a missed rename names itself. Populated on load,
    /// so it reflects the scenes loaded so far in this session.
    static std::vector<std::pair<std::string, std::string>> CollectUnknownProperties();

    /// Forget everything collected so far, so a check can scope itself to the
    /// scenes it is about to load. Clears the legacy list too.
    static void ClearUnknownProperties();

    /// Property keys read through a DEKI_RENAMED_FROM alias since the last
    /// clear, as {component type name, old key}: the file still carries the
    /// name from before a rename. A save writes the current name, so this is
    /// a warning ("re-save to migrate"), not an error.
    static std::vector<std::pair<std::string, std::string>> CollectLegacyProperties();

    /// Every reflected property of `comp` - its base class's first, then its
    /// own - as the JSON a scene file holds for it, keyed by reflected name.
    /// The same writer SaveToFile uses, so a value prints exactly as it would
    /// be saved. The docs build reads default values through this.
    static nlohmann::json ComponentPropertiesToJson(const Component* comp,
                                                    const ComponentMeta& meta);

    /// ComponentPropertiesToJson for a default-constructed instance of `meta`.
    /// Null when the type is abstract or has no factory.
    static nlohmann::json ComponentDefaultsToJson(const ComponentMeta& meta);

    // Editor-only: notification fired immediately before DestroyDekiObject deletes
    // an object (and its descendants). The vector contains the about-to-die set in
    // pre-order. Subscribers (selection, panel caches) can drop their pointers while
    // the objects are still valid.
    using OnObjectsAboutToBeDestroyedFn = std::function<void(const std::vector<Object*>&)>;
    /// Out of line: the callback is stored in a side table in Scene.cpp, not in
    /// a member, so that sizeof(Scene) does not depend on DEKI_EDITOR.
    /// std::function is 32 bytes here - too much to carry unconditionally on an
    /// embedded target that can never invoke it.
    void SetOnObjectsAboutToBeDestroyed(OnObjectsAboutToBeDestroyedFn fn);
#endif

    // =========================================================================
    // Getters/Setters
    // =========================================================================

    const std::string& GetName() const { return name; }
    void SetName(const std::string& sceneName) { name = sceneName; }

    bool IsLoaded() const { return m_IsLoaded; }

private:
    std::string name;
    std::vector<Object*> objects;               // DekiObjects in the scene
    std::vector<Component*> m_ComponentsCache;  // components of active objects, for the lifecycle
    bool m_ComponentsCacheDirty = true;           // Dirty flag for lazy rebuild
    uint32_t nextDekiObjectId;
    bool m_IsLoaded = false;

    // Stored data for re-instantiation
    std::vector<uint8_t> m_SceneData;

    // Rebuild the components cache (internal, called lazily)
    void RebuildComponentsCache();

    // Scene loading helpers
    void ClearScene();

#ifdef DEKI_EDITOR
    // Instance-path helpers. Internal to the instance expand/collapse and
    // override machinery above - no caller outside this class.
    static std::string BuildObjectPath(Object* obj, Object* instanceRoot);
    Object* FindObjectByPath(Object* root, const std::string& path);

    // Editor (JSON) serialization helpers
    nlohmann::json SaveObjectToJson(const Object* obj) const;
    void SavePropertyToJson(const Component* comp, const PropertyInfo& prop, nlohmann::json& propsJson) const;
    Object* LoadObjectFromJson(const nlohmann::json& json);
    void LoadPropertyFromJson(Component* comp, const PropertyInfo& prop, const nlohmann::json& propsJson);
#endif
};

}  // namespace Deki
