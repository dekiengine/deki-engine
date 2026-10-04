#pragma once
#include <algorithm>  // std::sort
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

// The namespace must open outside the editor guard, or runtime builds (no
// DEKI_EDITOR) get Scene at global scope with an unbalanced brace.
namespace Deki
{

#ifdef DEKI_EDITOR
struct ComponentMeta;  // reflection/Property.h; the docs-facing helpers take it by reference
struct PropertyInfo;
#endif

/// A scene: objects with components, and Deki's basic unit of content. A
/// scene can be loaded as the running scene, instanced inside another scene
/// (what other engines call a prefab), or shipped and loaded at runtime. There
/// is no separate prefab type.
class Scene
{
public:
    /// Asset type name for AssetManager::Load<T>().
    static constexpr const char* kAssetTypeName = "Scene";

    Scene();
    ~Scene();

    // =========================================================================
    // Static loading methods
    // =========================================================================

    /// Loads a scene through AssetManager. `path` is asset-relative without
    /// extension (such as "scenes/settings/settings"). Always returns a fresh
    /// Scene, or nullptr on failure. With `cache`, the raw file bytes stay in
    /// RAM so later loads skip the disk; the cached bytes are never modified.
    static Scene* Load(const char* path, bool cache = false);

    /// Loads a compiled .scene (binary MessagePack) from a resolved filesystem
    /// path, or returns nullptr. For AssetManager and SceneSystem; user code
    /// should call Load().
    static Scene* LoadInternal(const char* path);

    /// Loads a scene from compiled bytes in memory, or returns nullptr.
    static Scene* LoadFromMemory(const uint8_t* data, size_t size);

    // =========================================================================
    // Instantiation (for nesting scenes)
    // =========================================================================

    /// Adds a copy of this scene's objects to `targetScene`. Returns the
    /// copy's root object, or nullptr on failure.
    Object* Instantiate(Scene* targetScene);

    /// Instantiate(targetScene), with the root object placed at (x, y).
    Object* Instantiate(Scene* targetScene, float x, float y);

    // =========================================================================
    // Object management
    // =========================================================================

    void AddObject(Object* obj);
    void RemoveObject(Object* obj);

    const std::vector<Object*>& GetObjects() const { return m_Objects; }

    // Every component of every active object, for the per-frame lifecycle.
    // Rebuilt lazily when dirty.
    const std::vector<Component*>& GetActiveComponents();

    // The cache is rebuilt on the next GetActiveComponents() call.
    void InvalidateComponentsCache() { m_ComponentsCacheDirty = true; }

    Object* CreateDekiObject(const std::string& name = "DekiObject");
    void DestroyDekiObject(Object* dekiObject);
    Object* FindDekiObject(const std::string& name);
    Object* FindDekiObject(uint32_t id);

    // Loads compiled scene bytes (MessagePack).
    bool LoadData(const uint8_t* data, size_t size);

#ifdef DEKI_EDITOR
    // For reordering.
    void InsertObjectAt(Object* obj, size_t index);
    int GetObjectIndex(Object* obj) const;

    // The editor's JSON scene file.
    bool SaveToFile(const std::string& filePath);
    bool LoadFromFile(const std::string& filePath);

    // Used to back up the scene during play mode.
    std::string SaveToJsonString();

    // Used by hot reload.
    bool LoadFromJsonString(const std::string& jsonStr);

    // Deep copy through a JSON round trip.
    Object* DuplicateObject(const Object* source);

    // Scene instances.
    static DEKI_ENGINE_API std::function<std::string(const std::string& guid)> s_GuidToPathResolver;
    bool ExpandInstance(Object* instanceObj);
    void CollapseInstance(Object* instanceObj);
    void ApplyOverrides(Object* instanceObj, const nlohmann::json& overrides);
    void CollectOverrides(const Object* instanceRoot, const Object* obj, nlohmann::json& overrides) const;
    /// CollectOverrides, telling whether there was a source to compare with.
    /// False when the source scene could not be found or loaded (a merge
    /// conflict, saved by a newer editor, missing for a moment); `overrides`
    /// is then empty, and saving it would drop the instance's overrides.
    bool TryCollectOverrides(const Object* instanceRoot, const Object* obj, nlohmann::json& overrides) const;

    /// Property keys found in a scene file that no component's current
    /// metadata knows, as {component type name, key}.
    ///
    /// Reflected member names are the scene-file keys, so renaming a member
    /// changes its key and the old value silently falls back to its default
    /// (the value is kept and written back on save, but nothing reads it).
    /// Load every scene in a project and check this is empty to catch a
    /// rename that lacks DEKI_RENAMED_FROM. Filled on load, so it covers the
    /// scenes loaded so far in this session.
    static std::vector<std::pair<std::string, std::string>> CollectUnknownProperties();

    /// Forgets everything collected so far, so a check can cover only the
    /// scenes it is about to load. Clears the legacy list too.
    static void ClearUnknownProperties();

    /// Property keys read through a DEKI_RENAMED_FROM alias since the last
    /// clear, as {component type name, old key}. A save writes the current
    /// name, so this is a warning ("re-save to migrate"), not an error.
    static std::vector<std::pair<std::string, std::string>> CollectLegacyProperties();

    /// Every reflected property of `comp` (its base class's first, then its
    /// own) as the JSON a scene file holds, keyed by reflected name. Uses the
    /// same writer as SaveToFile, so values print exactly as saved. The docs
    /// build reads default values through this.
    static nlohmann::json ComponentPropertiesToJson(const Component* comp, const ComponentMeta& meta);

    /// ComponentPropertiesToJson for a default-constructed instance of `meta`.
    /// Null when the type is abstract or has no factory.
    static nlohmann::json ComponentDefaultsToJson(const ComponentMeta& meta);

    // Called right before DestroyDekiObject deletes an object and its
    // descendants, with them in pre-order, so subscribers (selection, panel
    // caches) can drop their pointers while the objects are still valid.
    using OnObjectsAboutToBeDestroyedFn = std::function<void(const std::vector<Object*>&)>;
    /// Out of line: the callback lives in a side table in Scene.cpp, not in a
    /// member, so sizeof(Scene) does not depend on DEKI_EDITOR and embedded
    /// targets do not carry a 32-byte std::function they never call.
    void SetOnObjectsAboutToBeDestroyed(OnObjectsAboutToBeDestroyedFn fn);
#endif

    // =========================================================================
    // Getters/Setters
    // =========================================================================

    const std::string& GetName() const { return m_Name; }
    void SetName(const std::string& sceneName) { m_Name = sceneName; }

    bool IsLoaded() const { return m_IsLoaded; }

private:
    std::string m_Name;
    std::vector<Object*> m_Objects;
    std::vector<Component*> m_ComponentsCache;  // components of active objects, for the lifecycle
    bool m_ComponentsCacheDirty = true;
    uint32_t m_NextDekiObjectId;
    bool m_IsLoaded = false;

    // The loaded bytes, kept for Instantiate.
    std::vector<uint8_t> m_SceneData;

    void RebuildComponentsCache();

    void ClearScene();

#ifdef DEKI_EDITOR
    // Helpers for instance expand/collapse and overrides.
    static std::string BuildObjectPath(Object* obj, Object* instanceRoot);
    Object* FindObjectByPath(Object* root, const std::string& path);

    // JSON serialization helpers.
    nlohmann::json SaveObjectToJson(const Object* obj) const;
    void SavePropertyToJson(const Component* comp, const PropertyInfo& prop, nlohmann::json& propsJson) const;
    Object* LoadObjectFromJson(const nlohmann::json& json);
    void LoadPropertyFromJson(Component* comp, const PropertyInfo& prop, const nlohmann::json& propsJson);
#endif
};

}  // namespace Deki
