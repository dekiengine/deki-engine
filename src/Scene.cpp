#include <deki/Scene.h>
#include "AuthoredFormat.h"

#include <algorithm>
#include <functional>
#include <typeinfo>
#include <cstring>
#include <unordered_map>

#include <deki/Object.h>
#include <deki/LogSystem.h>
#include <deki/providers/FileSystem.h>
#include <deki/providers/IFileSystem.h>
#include <deki/SceneMessagePack.h>
#include "MissingComponent.h"
#include "ObjectInternal.h"
#include "reflection/FormerTypeIds.h"
#include <deki/Time.h>
#include <deki/assets/AssetManager.h>

#ifdef DEKI_EDITOR
#include <fstream>
#include <filesystem>
#include <set>
#include <system_error>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include <deki/reflection/ComponentRegistry.h>
#include <deki/reflection/Property.h>
#include <deki/reflection/ObjectRef.h>
#include <deki/assets/AssetRef.h>
#include <deki/Color.h>
#include <deki/Vector.h>
#endif

namespace Deki
{

#ifdef DEKI_EDITOR

namespace
{
// Write `content` to `path` without ever leaving the destination truncated.
//
// A scene is the user's source asset — the only copy of their work. Streaming
// straight into it opens a window where a crash, a kill, or a full disk leaves
// a zero-byte or half-written file with the original already gone. Writing to a
// sibling temp file and renaming it over the destination closes that window:
// either the old content survives intact or the new content lands whole.
//
// The stream is also flushed and checked *before* the rename. std::ofstream
// buffers, so testing good() while the buffer is still pending reports success
// for a write that never reached the disk — which would let the editor clear
// its dirty flag over a file it failed to save.
bool AtomicWriteTextFile(const std::string& path, const std::string& content)
{
    namespace fs = std::filesystem;

    fs::path target(path);
    fs::path tmp = target;
    tmp += ".tmp";

    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            DEKI_LOG_ERROR("AtomicWriteTextFile: cannot open temp file '%s'", tmp.string().c_str());
            return false;
        }

        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        file.flush();

        if (!file.good())
        {
            DEKI_LOG_ERROR("AtomicWriteTextFile: write failed for '%s' (disk full or read-only?)",
                           tmp.string().c_str());
            file.close();
            std::error_code rmEc;
            fs::remove(tmp, rmEc);
            return false;
        }

        file.close();
        if (file.fail())
        {
            DEKI_LOG_ERROR("AtomicWriteTextFile: close failed for '%s'", tmp.string().c_str());
            std::error_code rmEc;
            fs::remove(tmp, rmEc);
            return false;
        }
    }

    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec)
    {
        // The temp file holds good content and the destination is untouched, so
        // leave both in place — the caller reports failure and the user's
        // previous save is still on disk.
        DEKI_LOG_ERROR("AtomicWriteTextFile: rename '%s' -> '%s' failed: %s",
                       tmp.string().c_str(), target.string().c_str(), ec.message().c_str());
        return false;
    }

    return true;
}
}  // namespace
#endif

#ifdef DEKI_EDITOR
namespace
{
// Side-table mapping a component pointer to the JSON property keys that
// were present in the scene file but unknown to the component's current
// metadata. Used to forward-carry property values across hot-reload when
// a new DLL adds a previously-unknown field.
//
// Lives outside Component so we don't change its ABI — plugin DLLs
// built against the old layout keep working.
using UnknownProps = std::unordered_map<std::string, nlohmann::json>;
std::unordered_map<const Component*, UnknownProps>& UnknownPropsTable()
{
    static std::unordered_map<const Component*, UnknownProps> table;
    return table;
}

// Diagnostic record of the same keys, as {component type name, key}.
//
// Deliberately NOT read back out of UnknownPropsTable: that table is keyed
// by component pointer and nothing erases a row when a component is
// destroyed, so after a scene is freed its keys are dangling. Reading them
// to report a type name is a use-after-free. This copies the name at load
// time, while the component is unquestionably alive, and holds no pointer.
std::vector<std::pair<std::string, std::string>>& UnknownPropsLog()
{
    static std::vector<std::pair<std::string, std::string>> log;
    return log;
}

// Keys read through a DEKI_RENAMED_FROM alias, same shape and lifetime rules.
std::vector<std::pair<std::string, std::string>>& LegacyPropsLog()
{
    static std::vector<std::pair<std::string, std::string>> log;
    return log;
}
}  // namespace

#endif

#ifdef DEKI_EDITOR
std::vector<std::pair<std::string, std::string>> Scene::CollectUnknownProperties()
{
    return UnknownPropsLog();
}

void Scene::ClearUnknownProperties()
{
    UnknownPropsLog().clear();
    LegacyPropsLog().clear();
}

std::vector<std::pair<std::string, std::string>> Scene::CollectLegacyProperties()
{
    return LegacyPropsLog();
}
#endif

// Layout tripwire — see the longer note in Object.cpp. Scene is milder than
// Object (packages hold Scene* and call out-of-line methods rather than
// reading members inline), but deki-editor.dll constructs one by value in
// AssetPipeline::CompileSceneFile, so the size must still agree across configs.
// Measured at 120 in both after the destroy-callback moved to a side table.
#if defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ == 8
static_assert(sizeof(Scene) == 120,
              "sizeof(Scene) changed. Verify the editor and a runtime config "
              "(-DDEKI_EDITOR=OFF -DSIMULATOR=ON) still agree, and bump "
              "kDekiReflectionAbiVersion in interop/ReflectionAbi.h.");
#endif

Scene::Scene()
    : nextDekiObjectId(1), m_IsLoaded(false) {}

#ifdef DEKI_EDITOR
namespace
{
// Destruction-notification callbacks, keyed by scene.
//
// Not a member, for the same reason Object's instance payload is not one:
// a std::function member would make sizeof(Scene) depend on DEKI_EDITOR.
// Same single-definition rule as UnknownPropsTable above — non-inline, one
// table per process.
//
// Only a handful of scenes are ever live and this is read once per
// DestroyDekiObject, so the lookup is nowhere near a hot path.
std::unordered_map<const Scene*, Scene::OnObjectsAboutToBeDestroyedFn>& DestroyCallbackTable()
{
    static std::unordered_map<const Scene*, Scene::OnObjectsAboutToBeDestroyedFn> table;
    return table;
}
}  // namespace

void Scene::SetOnObjectsAboutToBeDestroyed(OnObjectsAboutToBeDestroyedFn fn)
{
    if (fn)
        DestroyCallbackTable()[this] = std::move(fn);
    else
        DestroyCallbackTable().erase(this);
}
#endif

Scene::~Scene()
{
#ifdef DEKI_EDITOR
    // Keyed by `this`; a stale row would fire a dead callback for whatever scene
    // is allocated here next.
    DestroyCallbackTable().erase(this);
#endif
    ClearScene();
}

void Scene::ClearScene()
{
    for (auto* obj : objects)
    {
        delete obj;
    }
    objects.clear();
    m_ComponentsCache.clear();
    m_ComponentsCacheDirty = true;
    m_IsLoaded = false;
}

// =============================================================================
// Static loading methods
// =============================================================================

Scene* Scene::Load(const char* path, bool cache)
{
    if (!path)
    {
        DEKI_LOG_ERROR("Scene::Load: Path is null");
        return nullptr;
    }

    auto* mgr = Deki::AssetManager::Get();
    if (!mgr)
        return nullptr;

    return mgr->Load<Scene>(path, cache);
}

Scene* Scene::LoadInternal(const char* path)
{
    if (!path)
    {
        DEKI_LOG_ERROR("Scene::LoadInternal: Path is null");
        return nullptr;
    }

    // Use FileSystem if available
    if (!FileSystem::IsInitialized())
    {
        DEKI_LOG_ERROR("Scene::LoadInternal: FileSystem not initialized");
        return nullptr;
    }

    IFileSystem* fs = FileSystem::GetFileSystemForPath(path);
    if (!fs)
    {
        DEKI_LOG_ERROR("Scene::LoadInternal: No filesystem available");
        return nullptr;
    }

    // Open file
    IFileSystem::FileHandle handle = fs->OpenFile(path, IFileSystem::OpenMode::READ_BINARY);
    if (!handle)
    {
        DEKI_LOG_ERROR("Scene::LoadInternal: Failed to open file '%s'", path);
        return nullptr;
    }

    // Get file size
    long fileSize = fs->GetFileSize(handle);
    if (fileSize <= 0)
    {
        fs->CloseFile(handle);
        DEKI_LOG_ERROR("Scene::LoadInternal: Invalid file size for '%s'", path);
        return nullptr;
    }

    // Read file directly into scene data (avoids double allocation + memcpy)
    Scene* scene = new Scene();
    scene->m_SceneData.resize(static_cast<size_t>(fileSize));
    size_t bytesRead = fs->ReadFile(handle, scene->m_SceneData.data(), static_cast<size_t>(fileSize));
    fs->CloseFile(handle);

    if (bytesRead != static_cast<size_t>(fileSize))
    {
        delete scene;
        DEKI_LOG_ERROR("Scene::LoadInternal: Failed to read file '%s'", path);
        return nullptr;
    }

    // Parse MessagePack data (works in both editor and runtime)
    SceneFormat::SceneMsgPackParser parser(scene->m_SceneData.data(), bytesRead);
    bool success = parser.ParseScene(scene);

    if (!success)
    {
        delete scene;
        DEKI_LOG_ERROR("Scene::LoadInternal: Failed to parse scene '%s'", path);
        return nullptr;
    }

    scene->m_IsLoaded = true;

    // Extract name from path
    std::string pathStr(path);
    size_t lastSlash = pathStr.find_last_of("/\\");
    if (lastSlash != std::string::npos)
    {
        scene->name = pathStr.substr(lastSlash + 1);
    }
    else
    {
        scene->name = path;
    }

    return scene;
}

Scene* Scene::LoadFromMemory(const uint8_t* data, size_t size)
{
    if (!data || size == 0)
    {
        DEKI_LOG_ERROR("Scene::LoadFromMemory: Invalid data");
        return nullptr;
    }

    Scene* scene = new Scene();
    if (!scene->LoadData(data, size))
    {
        delete scene;
        return nullptr;
    }

    // Store data for potential re-instantiation
    scene->m_SceneData.resize(size);
    memcpy(scene->m_SceneData.data(), data, size);
    scene->m_IsLoaded = true;

    return scene;
}

// =============================================================================
// Instantiation
// =============================================================================

Object* Scene::Instantiate(Scene* targetScene)
{
    return Instantiate(targetScene, 0.0f, 0.0f);
}

Object* Scene::Instantiate(Scene* targetScene, float x, float y)
{
    if (!m_IsLoaded || m_SceneData.empty())
    {
        DEKI_LOG_ERROR("Scene::Instantiate: Scene not loaded");
        return nullptr;
    }

    if (!targetScene)
    {
        DEKI_LOG_ERROR("Scene::Instantiate: Target scene is null");
        return nullptr;
    }

    // Single-root model: spawned objects must live under the target's root,
    // not as a top-level sibling.
    auto& targetObjects = targetScene->GetObjects();
    if (targetObjects.empty())
    {
        DEKI_LOG_ERROR("Scene::Instantiate: Target scene has no root object");
        return nullptr;
    }
    Object* targetRoot = targetObjects[0];

    // Parse the full scene format into a temporary scene
    Scene tempScene;
    SceneFormat::SceneMsgPackParser parser(m_SceneData.data(), m_SceneData.size());
    if (!parser.ParseScene(&tempScene))
    {
        DEKI_LOG_ERROR("Scene::Instantiate: Failed to parse scene data");
        return nullptr;
    }

    // Get the first root object
    auto& tempObjects = tempScene.GetObjects();
    if (tempObjects.empty())
    {
        DEKI_LOG_ERROR("Scene::Instantiate: Scene has no root objects");
        return nullptr;
    }

    // Detach root from temp scene so its destructor won't delete it
    Object* root = tempObjects[0];
    tempScene.RemoveObject(root);

    // Apply position offset to the root object's transform
#ifdef DEKI_TRANSFORM_2D
    root->SetLocalPosition(root->GetX() + x, root->GetY() + y);
#else
    (void)x;
    (void)y;
#endif

    // Fix ownerScene on the entire spawned subtree — LoadObjectFromJson set
    // them to &tempScene, which is about to be destroyed. Do this before
    // AddChild so the propagation check inside AddChild sees consistent state.
    VisitSubtree(root, [targetScene](Object* obj)
                 { obj->SetOwnerScene(targetScene); });

    // Attach under the target's root. The Behaviour lifecycle is picked
    // up via the behaviours cache rebuild on the next frame.
    targetRoot->AddChild(root);
    targetScene->InvalidateComponentsCache();

    // Remap object IDs to avoid duplicates when multiple instances exist
    std::unordered_map<uint32_t, uint32_t> idRemap;
    VisitSubtree(root, [&idRemap](Object* obj)
                 {
        uint32_t oldId = obj->GetId();
        uint32_t newId = Object::GenerateId();
        idRemap[oldId] = newId;
        obj->SetId(newId); });

    // Update ObjectRef fields to point to remapped IDs
    VisitSubtree(root, [&idRemap](Object* obj)
                 {
        for (auto* comp : obj->GetComponents())
            comp->RemapObjectRefs(idRemap); });

    // Resolve ObjectRefs immediately so they're available before the next lifecycle tick
    // (some components spawn objects in Start() and need their refs already resolved
    // when their Start() body runs in the same frame).
    VisitSubtree(root, [targetScene](Object* obj)
                 {
        for (auto* comp : obj->GetComponents())
        {
            if (!comp->HasRefsResolved())
            {
                comp->ResolveObjectRefs(targetScene);
                comp->MarkRefsResolved();
            }
        } });

    return root;
}

// =============================================================================
// Object management
// =============================================================================

void Scene::AddObject(Object* obj)
{
    // Set owner_scene on the entire subtree, not just the root. Loaders that
    // build the tree before adding it (e.g. SceneMsgPackParser::ParseObjectInternal)
    // only call AddChild on descendants — without this, child objects loaded
    // via the runtime MessagePack path would have null owner_scene, breaking
    // any code that walks back up the scene from a child (anything that calls
    // GetOwnerScene() on a non-root object).
    VisitSubtree(obj, [this](Object* o)
                 { o->SetOwnerScene(this); });
    objects.push_back(obj);
    DEKI_LOG_INTERNAL("Scene::AddObject: Added '%s', objects.size()=%zu, this=%p",
                      obj ? obj->GetName().c_str() : "null", objects.size(), (void*)this);
    m_ComponentsCacheDirty = true;
}

Object* Scene::CreateDekiObject(const std::string& name)
{
    Object* obj = new Object(name);
    AddObject(obj);
    return obj;
}

void Scene::DestroyDekiObject(Object* obj)
{
    if (!obj) return;

#ifdef DEKI_EDITOR
    // Notify subscribers (editor selection, panel caches) before pointers go invalid.
    // Walk the subtree in pre-order so the callback sees obj first, then its descendants.
    auto& destroyCallbacks = DestroyCallbackTable();
    auto callbackIt = destroyCallbacks.find(this);
    if (callbackIt != destroyCallbacks.end() && callbackIt->second)
    {
        std::vector<Object*> dying;
        VisitSubtree(obj, [&dying](Object* o)
                     { dying.push_back(o); });
        callbackIt->second(dying);
    }
#endif

    // Remove from parent if has one
    obj->RemoveFromParent();

    // Remove from scene
    RemoveObject(obj);

    // Delete the object
    delete obj;
}

void Scene::RemoveObject(Object* obj)
{
    if (obj) obj->SetOwnerScene(nullptr);
    objects.erase(std::remove(objects.begin(), objects.end(), obj), objects.end());
    m_ComponentsCacheDirty = true;
}

Object* Scene::FindDekiObject(const std::string& name)
{
    // Whole tree, like the id overload. This only looked at the root list,
    // and since every scene is migrated to a single "Root", it could find
    // nothing but Root.
    for (auto* obj : objects)
    {
        Object* found = FindInSubtree(obj, [&name](Object* o)
                                          { return o->GetName() == name; });
        if (found) return found;
    }
    return nullptr;
}

Object* Scene::FindDekiObject(uint32_t id)
{
    for (auto* obj : objects)
    {
        Object* found = FindInSubtree(obj, [id](Object* o)
                                          { return o->GetId() == id; });
        if (found) return found;
    }
    return nullptr;
}

const std::vector<Component*>& Scene::GetActiveComponents()
{
    if (m_ComponentsCacheDirty)
    {
        RebuildComponentsCache();
    }
    return m_ComponentsCache;
}

void Scene::RebuildComponentsCache()
{
    m_ComponentsCache.clear();

    // Inactive objects are skipped along with their whole subtree.
    for (auto* obj : objects)
    {
        VisitActiveSubtree(obj, [this](Object* o)
                           {
            for (auto* comp : o->GetComponents())
                m_ComponentsCache.push_back(comp); });
    }

    m_ComponentsCacheDirty = false;
}

// =============================================================================
// Runtime loading (MessagePack for embedded)
// =============================================================================

#ifndef DEKI_EDITOR

bool Scene::LoadData(const uint8_t* data, size_t size)
{
    ClearScene();
    SceneFormat::SceneMsgPackParser parser(data, size);
    bool success = parser.ParseScene(this);
    if (success) m_IsLoaded = true;
    return success;
}

#endif  // !DEKI_EDITOR

// =============================================================================
// Editor loading/saving (JSON for human readability)
// =============================================================================

#ifdef DEKI_EDITOR

void Scene::InsertObjectAt(Object* obj, size_t index)
{
    if (!obj) return;
    obj->SetOwnerScene(this);

    if (index > objects.size())
        index = objects.size();

    objects.insert(objects.begin() + index, obj);
    // Note: Do NOT call SortObjectsByOrder() here - we want manual ordering
    m_ComponentsCacheDirty = true;
}

int Scene::GetObjectIndex(Object* obj) const
{
    auto it = std::find(objects.begin(), objects.end(), obj);
    if (it != objects.end())
        return static_cast<int>(std::distance(objects.begin(), it));
    return -1;
}

bool Scene::LoadData(const uint8_t* data, size_t size)
{
    ClearScene();

    try
    {
        std::string jsonStr(reinterpret_cast<const char*>(data), size);
        nlohmann::json json = nlohmann::json::parse(jsonStr);

        // A file from a newer editor is refused whole, not loaded partially: a
        // half-understood scene saved back would lose whatever the newer format
        // carried. Files without the key predate it and are version 1.
        const int32_t fileVersion = json.value(Deki::AuthoredFormat::VersionKey, 1);
        if (fileVersion > Deki::AuthoredFormat::SceneVersion)
        {
            DEKI_LOG_ERROR("Scene: file is formatVersion %d but this editor reads up to %d; update the editor",
                           fileVersion, Deki::AuthoredFormat::SceneVersion);
            ClearScene();
            return false;
        }

        // Note: name and clearColor are no longer stored in Scene
        // clearColor is now in CameraComponent

        if (json.contains("objects"))
        {
            for (const auto& objJson : json["objects"])
            {
                LoadObjectFromJson(objJson);
            }
        }

        // Migration: enforce single-root model
        // If scene has zero or multiple roots, wrap them under a "Root" container
        if (objects.size() != 1)
        {
            Object* root = new Object("Root");
            root->SetOwnerScene(this);
            for (auto* obj : objects)
                root->AddChild(obj);
            objects.clear();
            objects.push_back(root);
        }

        // Recalculate all world transforms after hierarchy is fully loaded
        for (Object* obj : objects)
        {
#ifdef DEKI_TRANSFORM_2D
            obj->SetLocalPosition(obj->GetX(), obj->GetY());
#endif
        }

        m_IsLoaded = true;
        return true;
    }
    catch (const std::exception& e)
    {
        // A malformed scene used to come back as a bare `false`: no reason, no
        // byte offset, nothing in the console.
        DEKI_LOG_ERROR("Scene::LoadData: failed to parse scene JSON (%zu bytes): %s", size, e.what());
        ClearScene();
        return false;
    }
}

bool Scene::SaveToFile(const std::string& filePath)
{
    nlohmann::json json;
    json[Deki::AuthoredFormat::VersionKey] = Deki::AuthoredFormat::SceneVersion;

    // Note: name and clearColor are no longer stored in Scene
    // clearColor is now in CameraComponent

    json["objects"] = nlohmann::json::array();
    for (const auto* obj : objects)
    {
        if (obj->IsEditorOnly())
            continue;  // Skip editor-only objects
        json["objects"].push_back(SaveObjectToJson(obj));
    }

    // Atomic: a scene is the user's only copy of this work, so the write must
    // never be able to truncate it and then fail. See AtomicWriteTextFile.
    return AtomicWriteTextFile(filePath, json.dump(2));
}

bool Scene::LoadFromFile(const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
        return false;

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    return LoadData(reinterpret_cast<const uint8_t*>(content.data()), content.size());
}

std::string Scene::SaveToJsonString()
{
    nlohmann::json json;
    json[Deki::AuthoredFormat::VersionKey] = Deki::AuthoredFormat::SceneVersion;

    // Note: name and clearColor are no longer stored in Scene
    // clearColor is now in CameraComponent

    json["objects"] = nlohmann::json::array();
    for (const auto* obj : objects)
    {
        if (obj->IsEditorOnly())
            continue;  // Skip editor-only objects
        json["objects"].push_back(SaveObjectToJson(obj));
    }

    return json.dump();
}

bool Scene::LoadFromJsonString(const std::string& jsonStr)
{
    return LoadData(reinterpret_cast<const uint8_t*>(jsonStr.data()), jsonStr.size());
}

nlohmann::json Scene::ComponentPropertiesToJson(const Component* comp, const ComponentMeta& meta)
{
    nlohmann::json props = nlohmann::json::object();
    if (!comp)
        return props;

    // SavePropertyToJson is a member for symmetry with the loader; it reads
    // nothing from the scene, so a scratch instance serves.
    Scene scratch;
    auto& registry = ComponentRegistry::Instance();
    if (meta.baseTypeId != 0)
    {
        if (const ComponentMeta* base = registry.GetMeta(meta.baseTypeId))
            for (int i = 0; i < base->propertyCount; ++i)
                scratch.SavePropertyToJson(comp, base->properties[i], props);
    }
    for (int i = 0; i < meta.propertyCount; ++i)
        scratch.SavePropertyToJson(comp, meta.properties[i], props);
    return props;
}

nlohmann::json Scene::ComponentDefaultsToJson(const ComponentMeta& meta)
{
    if (meta.isAbstract || !meta.createFunc)
        return nullptr;
    Component* comp = meta.createFunc();
    if (!comp)
        return nullptr;
    nlohmann::json props = ComponentPropertiesToJson(comp, meta);
    delete comp;
    return props;
}

nlohmann::json Scene::SaveObjectToJson(const Object* obj) const
{
    nlohmann::json json;

    json["id"] = obj->GetId();
    json["name"] = obj->GetName();
    json["active"] = obj->IsActive();

#ifdef DEKI_TRANSFORM_2D
    json["transform"] = {
        { "x", obj->GetX() },
        { "y", obj->GetY() },
        { "rotation", obj->GetLocalRotation() },
        { "scaleX", obj->GetScaleX() },
        { "scaleY", obj->GetScaleY() }
    };
#ifdef DEKI_TRANSFORM_3D
    // Written only when set, so a 2D project's file is byte for byte what it
    // was, and a 2D device skips the keys it does not know.
    if (obj->GetZ() != 0.0f)
        json["transform"]["z"] = obj->GetZ();
    if (obj->GetRotationX() != 0.0f)
        json["transform"]["rotationX"] = obj->GetRotationX();
    if (obj->GetRotationY() != 0.0f)
        json["transform"]["rotationY"] = obj->GetRotationY();
    if (obj->GetScaleZ() != 1.0f)
        json["transform"]["scaleZ"] = obj->GetScaleZ();
#endif
#endif

    // Scene instance: save only the link + collected overrides
    if (obj->IsSceneInstance())
    {
        json["instance"] = obj->GetSourceSceneGuid();

        // Collect overrides by diffing current state against source
        nlohmann::json overrides = nlohmann::json::array();
        CollectOverrides(obj, obj, overrides);
        if (!overrides.empty())
        {
            json["overrides"] = overrides;
            // Also store back on the object for display purposes
            const_cast<Object*>(obj)->SetInstanceOverridesJson(overrides.dump());
        }
        return json;
    }

    // Save components
    json["components"] = nlohmann::json::array();
    auto& registry = ComponentRegistry::Instance();

    for (const auto* comp : obj->GetComponents())
    {
        nlohmann::json compJson;

        // MissingComponent — write back preserved type name and JSON properties
        if (comp->GetType() == MissingComponent::StaticType)
        {
            auto* missing = static_cast<const MissingComponent*>(comp);
            compJson["type"] = missing->m_TypeName;
            if (!missing->m_ClassName.empty())
                compJson["class"] = missing->m_ClassName;
            compJson["properties"] = missing->m_JsonProperties;
            json["components"].push_back(compJson);
            continue;
        }

        const ComponentMeta* meta = registry.GetMeta(comp->GetType());

        if (meta)
        {
            compJson["type"] = meta->serializedName;
            // "class" is no longer written. It was the 0.10-era hint for
            // resolving a component whose type had moved, and since identity
            // became the qualified class name it has held the same string as
            // "type" with a namespace stripped off — a second name that can
            // drift from the first and says nothing the first does not.
            // Still READ, below, for the files that have it, and still written
            // for a component whose package is missing, where it is the only
            // record of what the type was called.
            compJson["properties"] = nlohmann::json::object();

            // Serialize base class properties first (e.g., sortingOrder from RendererComponent)
            if (meta->baseTypeId != 0)
            {
                const ComponentMeta* baseMeta = registry.GetMeta(meta->baseTypeId);
                if (baseMeta && baseMeta->propertyCount > 0)
                {
                    for (int i = 0; i < baseMeta->propertyCount; ++i)
                    {
                        SavePropertyToJson(comp, baseMeta->properties[i], compJson["properties"]);
                    }
                }
            }

            // Serialize derived class properties using reflection
            for (int i = 0; i < meta->propertyCount; ++i)
            {
                const PropertyInfo& prop = meta->properties[i];
                SavePropertyToJson(comp, prop, compJson["properties"]);
            }

            // Forward-compat: re-emit any property keys captured on load that
            // weren't recognised by the current metadata. Skip any that have
            // since become known (a hot-reloaded DLL added them).
            auto& table = UnknownPropsTable();
            auto it = table.find(comp);
            if (it != table.end())
            {
                for (const auto& kv : it->second)
                {
                    if (!compJson["properties"].contains(kv.first))
                        compJson["properties"][kv.first] = kv.second;
                }
            }
        }
        else
        {
            compJson["type"] = "Unknown";
            compJson["typeId"] = comp->GetType();
        }

        json["components"].push_back(compJson);
    }

    // Save children recursively
    json["children"] = nlohmann::json::array();
    for (const auto* child : obj->GetChildren())
    {
        if (child->IsEditorOnly())
            continue;  // Skip editor-only objects
        json["children"].push_back(SaveObjectToJson(child));
    }

    return json;
}

void Scene::SavePropertyToJson(const Component* comp, const PropertyInfo& prop, nlohmann::json& propsJson) const
{
    switch (prop.type)
    {
        case PropertyType::Int8:
            propsJson[prop.name] = static_cast<int32_t>(GetPropertyValue<int8_t>(comp, prop));
            break;
        case PropertyType::Int16:
            propsJson[prop.name] = static_cast<int32_t>(GetPropertyValue<int16_t>(comp, prop));
            break;
        case PropertyType::Int32:
            propsJson[prop.name] = GetPropertyValue<int32_t>(comp, prop);
            break;
        case PropertyType::Int64:
            propsJson[prop.name] = GetPropertyValue<int64_t>(comp, prop);
            break;
        case PropertyType::UInt8:
            propsJson[prop.name] = static_cast<int32_t>(GetPropertyValue<uint8_t>(comp, prop));
            break;
        case PropertyType::UInt16:
            propsJson[prop.name] = static_cast<int32_t>(GetPropertyValue<uint16_t>(comp, prop));
            break;
        case PropertyType::UInt32:
            propsJson[prop.name] = GetPropertyValue<uint32_t>(comp, prop);
            break;
        case PropertyType::UInt64:
            propsJson[prop.name] = GetPropertyValue<uint64_t>(comp, prop);
            break;
        case PropertyType::Float:
            propsJson[prop.name] = GetPropertyValue<float>(comp, prop);
            break;
        case PropertyType::Double:
            propsJson[prop.name] = GetPropertyValue<double>(comp, prop);
            break;
        case PropertyType::Bool:
            propsJson[prop.name] = GetPropertyValue<bool>(comp, prop);
            break;
        case PropertyType::String:
            propsJson[prop.name] = GetPropertyValue<std::string>(comp, prop);
            break;
        case PropertyType::Color:
        {
            // "#RRGGBBAA". A packed number is unreadable in a diff and freezes
            // the channel order into every saved file. The compiled scene the
            // device loads keeps the packed form: that is build output.
            char hex[10];
            ColorToHex(GetPropertyRef<Deki::Color>(comp, prop), hex);
            propsJson[prop.name] = hex;
        }
            break;
        case PropertyType::Enum:
        {
            // Use enumSize to read the correct number of bytes
            int32_t enumValue = 0;
            switch (prop.enumSize)
            {
                case 1:
                    enumValue = static_cast<int32_t>(GetPropertyValue<uint8_t>(comp, prop));
                    break;
                case 2:
                    enumValue = static_cast<int32_t>(GetPropertyValue<uint16_t>(comp, prop));
                    break;
                default:
                    enumValue = GetPropertyValue<int32_t>(comp, prop);
                    break;
            }
            // Write the enumerator's NAME. A bare number would mean whatever sat
            // at that position when the file was written, so appending to an
            // enum, or reordering one, silently changed every saved scene. The
            // name survives both. A value the enum does not declare has no name
            // to write, so it falls back to the number rather than being lost.
            if (const char* enumName = EnumNameForValue(prop, enumValue))
                propsJson[prop.name] = enumName;
            else
                propsJson[prop.name] = enumValue;
            break;
        }
        case PropertyType::Asset:
            // Asset paths are stored as strings
            propsJson[prop.name] = GetPropertyValue<std::string>(comp, prop);
            break;
        case PropertyType::ObjectRef:
        {
            const ObjectRefBase& ref = GetPropertyRef<ObjectRefBase>(comp, prop);
            if (ref.componentTypeId != 0)
            {
                propsJson[prop.name] = nlohmann::json{ { "id", ref.id }, { "typeId", ref.componentTypeId } };
            }
            else
            {
                propsJson[prop.name] = ref.id;
            }
            break;
        }
        case PropertyType::Array:
        {
            // Serialize array based on element type
            nlohmann::json arr = nlohmann::json::array();
            const void* ptr = reinterpret_cast<const char*>(comp) + prop.offset;

            switch (prop.elementType)
            {
                case PropertyType::String:
                {
                    const auto& vec = *reinterpret_cast<const std::vector<std::string>*>(ptr);
                    for (const auto& str : vec)
                        arr.push_back(str);
                    break;
                }
                case PropertyType::Int32:
                {
                    const auto& vec = *reinterpret_cast<const std::vector<int32_t>*>(ptr);
                    for (auto v : vec)
                        arr.push_back(v);
                    break;
                }
                case PropertyType::Float:
                {
                    const auto& vec = *reinterpret_cast<const std::vector<float>*>(ptr);
                    for (auto v : vec)
                        arr.push_back(v);
                    break;
                }
                case PropertyType::Bool:
                {
                    const auto& vec = *reinterpret_cast<const std::vector<bool>*>(ptr);
                    for (auto v : vec)
                        arr.push_back(v);
                    break;
                }
                case PropertyType::ObjectRef:
                {
                    const auto& vec = *reinterpret_cast<const std::vector<ObjectRefBase>*>(ptr);
                    for (const auto& ref : vec)
                    {
                        if (ref.componentTypeId != 0)
                            arr.push_back(nlohmann::json{ { "id", ref.id }, { "typeId", ref.componentTypeId } });
                        else
                            arr.push_back(ref.id);
                    }
                    break;
                }
                default:
                    break;
            }
            propsJson[prop.name] = arr;
            break;
        }
        case PropertyType::AssetRef:
        {
            // AssetRef stores GUID in the guid field, and optionally source for derived assets
            const Deki::AssetRefBase& ref = GetPropertyRef<Deki::AssetRefBase>(comp, prop);
            if (ref.source.empty())
            {
                // Backward compatible: simple string if no source
                propsJson[prop.name] = ref.guid;
            }
            else
            {
                // Store both guid and source
                nlohmann::json obj;
                obj["guid"] = ref.guid;
                obj["source"] = ref.source;
                propsJson[prop.name] = obj;
            }
            break;
        }
        case PropertyType::Vector2:
        {
            const Vector2* ptr = reinterpret_cast<const Vector2*>(
                reinterpret_cast<const char*>(comp) + prop.offset);
            propsJson[prop.name] = nlohmann::json::array({ ptr->x, ptr->y });
            break;
        }
        case PropertyType::Vector3:
        {
            const Vector3* ptr = reinterpret_cast<const Vector3*>(
                reinterpret_cast<const char*>(comp) + prop.offset);
            propsJson[prop.name] = nlohmann::json::array({ ptr->x, ptr->y, ptr->z });
            break;
        }
        default:
            break;
    }
}

Object* Scene::LoadObjectFromJson(const nlohmann::json& json)
{
    std::string name = json.value("name", "DekiObject");
    Object* obj = CreateDekiObject(name);

    // Restore saved object ID to preserve ObjectRef references
    if (json.contains("id"))
    {
        obj->SetId(json["id"].get<uint32_t>());
    }

    obj->SetActive(json.value("active", true));
    // Note: sortingOrder is now loaded via RendererComponent properties

#ifdef DEKI_TRANSFORM_2D
    if (json.contains("transform"))
    {
        auto& t = json["transform"];
        obj->SetLocalPosition(t.value("x", 0.0f), t.value("y", 0.0f));
        obj->SetRotation(t.value("rotation", 0.0f));
        obj->SetScale(t.value("scaleX", 1.0f), t.value("scaleY", 1.0f));
#ifdef DEKI_TRANSFORM_3D
        obj->SetZ(t.value("z", 0.0f));
        obj->SetRotationX(t.value("rotationX", 0.0f));
        obj->SetRotationY(t.value("rotationY", 0.0f));
        obj->SetScaleZ(t.value("scaleZ", 1.0f));
#endif
    }
#endif

    // Scene instance: set source GUID and expand from source file
    if (json.contains("instance") && json["instance"].is_string())
    {
        obj->SetSourceSceneGuid(json["instance"].get<std::string>());
        if (json.contains("overrides"))
        {
            obj->SetInstanceOverridesJson(json["overrides"].dump());
        }
        if (!ExpandInstance(obj))
        {
            DEKI_LOG_ERROR("Scene: Failed to expand instance '%s' (source GUID: %s)",
                           name.c_str(), obj->GetSourceSceneGuid().c_str());
        }
        // Preserve the saved name (ExpandInstance overwrites with source root's name,
        // but the user may have renamed the instance)
        obj->SetName(name);
        return obj;
    }

    // Load components
    auto& registry = ComponentRegistry::Instance();
    if (json.contains("components"))
    {
        for (const auto& compJson : json["components"])
        {
            std::string typeName = compJson.value("type", "");
            std::string classHint = compJson.value("class", "");
            Component* comp = Internal::AddComponentByName(*obj, typeName, classHint);

            if (comp)
            {
                // Load properties using reflection
                const ComponentMeta* meta = registry.GetMeta(comp->GetType());  // not typeName: a stale GUID resolved by class name
                if (meta && compJson.contains("properties"))
                {
                    // Load base class properties first (e.g., sortingOrder from RendererComponent)
                    if (meta->baseTypeId != 0)
                    {
                        const ComponentMeta* baseMeta = registry.GetMeta(meta->baseTypeId);
                        if (baseMeta && baseMeta->propertyCount > 0)
                        {
                            for (int i = 0; i < baseMeta->propertyCount; ++i)
                            {
                                LoadPropertyFromJson(comp, baseMeta->properties[i], compJson["properties"]);
                            }
                        }
                    }

                    // Load derived class properties
                    for (int i = 0; i < meta->propertyCount; ++i)
                    {
                        const PropertyInfo& prop = meta->properties[i];
                        LoadPropertyFromJson(comp, prop, compJson["properties"]);
                    }

                    // Forward-compat: stash any property keys the current metadata
                    // doesn't know about, so a hot-reloaded DLL with new fields can
                    // pick them up on the next save+load. Stored in a side-table
                    // so Component's ABI stays unchanged for plugin DLLs.
                    std::unordered_set<std::string> known;
                    // A DEKI_RENAMED_FROM alias is a known key too — read by
                    // LoadPropertyFromJson when the current name is absent — and
                    // that case is recorded as legacy so a check can ask for a
                    // re-save. A file carrying both keys is fine: the new one won.
                    auto noteProps = [&](const ComponentMeta* m)
                    {
                        for (int i = 0; i < m->propertyCount; ++i)
                        {
                            const PropertyInfo& p = m->properties[i];
                            if (!p.name) continue;
                            known.insert(p.name);
                            if (p.renamedFrom && *p.renamedFrom)
                            {
                                known.insert(p.renamedFrom);
                                const auto& props = compJson["properties"];
                                if (props.contains(p.renamedFrom) && !props.contains(p.name))
                                    LegacyPropsLog().emplace_back(meta->name ? meta->name : "<unnamed>", p.renamedFrom);
                            }
                        }
                    };
                    if (meta->baseTypeId != 0)
                    {
                        const ComponentMeta* baseMeta = registry.GetMeta(meta->baseTypeId);
                        if (baseMeta)
                            noteProps(baseMeta);
                    }
                    noteProps(meta);

                    UnknownProps unknownsForComp;
                    for (auto it = compJson["properties"].begin(); it != compJson["properties"].end(); ++it)
                    {
                        if (known.find(it.key()) == known.end())
                            unknownsForComp[it.key()] = it.value();
                    }
                    if (!unknownsForComp.empty())
                    {
                        auto& log = UnknownPropsLog();
                        for (const auto& kv : unknownsForComp)
                            log.emplace_back(meta->name ? meta->name : "<unnamed>", kv.first);
                        UnknownPropsTable()[comp] = std::move(unknownsForComp);
                    }
                    else
                        UnknownPropsTable().erase(comp);
                }
            }
            else if (!typeName.empty() && typeName != "Unknown")
            {
                // Component type not found — create MissingComponent to preserve data
                auto* missing = new MissingComponent();
                missing->m_TypeName = typeName;
                missing->m_ClassName = classHint;
                missing->m_OriginalTypeId = HashString(typeName.c_str());
                if (compJson.contains("properties"))
                {
                    missing->m_JsonProperties = compJson["properties"];
                }
                Internal::AttachComponent(*obj, missing);
            }
        }
    }

    // Load children recursively
    if (json.contains("children"))
    {
        for (const auto& childJson : json["children"])
        {
            Object* child = LoadObjectFromJson(childJson);
            if (child)
            {
                // Remove from root objects list
                RemoveObject(child);
                child->SetOwnerScene(this);
                obj->AddChild(child);
                // Update world transform now that parent is set
#ifdef DEKI_TRANSFORM_2D
                child->SetLocalPosition(child->GetX(), child->GetY());
#endif
            }
        }
    }

    return obj;
}

Object* Scene::DuplicateObject(const Object* source)
{
    if (!source) return nullptr;

    nlohmann::json objJson = SaveObjectToJson(source);
    Object* cloned = LoadObjectFromJson(objJson);
    if (cloned)
    {
        // LoadObjectFromJson adds to root objects list, remove it
        RemoveObject(cloned);
        cloned->SetOwnerScene(this);

        // The JSON round-trip copies ids too, so every duplicate - and each
        // object under it - used to share its original's id: two objects
        // answering to one id, and a saved scene full of them. Fresh ids for
        // the whole copy, and its references to objects inside it moved to
        // the copies, as spawning a prefab already does. References out of
        // the copy keep pointing where the original's did.
        std::unordered_map<uint32_t, uint32_t> idRemap;
        VisitSubtree(cloned, [&idRemap](Object* obj)
                     {
            const uint32_t newId = Object::GenerateId();
            idRemap[obj->GetId()] = newId;
            obj->SetId(newId); });
        VisitSubtree(cloned, [&idRemap](Object* obj)
                     {
            for (auto* comp : obj->GetComponents())
                comp->RemapObjectRefs(idRemap); });
    }
    return cloned;
}

void Scene::LoadPropertyFromJson(Component* comp, const PropertyInfo& prop, const nlohmann::json& propsJson)
{
    if (!prop.name)
        return;

    // The current name wins; a file from before a rename is read through the
    // DEKI_RENAMED_FROM alias and written back under the current name on save.
    const char* key = prop.name;
    if (!propsJson.contains(key))
    {
        if (!prop.renamedFrom || !*prop.renamedFrom || !propsJson.contains(prop.renamedFrom))
            return;
        key = prop.renamedFrom;
    }

    const auto& value = propsJson[key];

    switch (prop.type)
    {
        case PropertyType::Int8:
            if (value.is_number_integer())
                SetPropertyValue<int8_t>(comp, prop, static_cast<int8_t>(value.get<int32_t>()));
            break;
        case PropertyType::Int16:
            if (value.is_number_integer())
                SetPropertyValue<int16_t>(comp, prop, static_cast<int16_t>(value.get<int32_t>()));
            break;
        case PropertyType::Int32:
            if (value.is_number_integer())
                SetPropertyValue<int32_t>(comp, prop, value.get<int32_t>());
            break;
        case PropertyType::Int64:
            if (value.is_number_integer())
                SetPropertyValue<int64_t>(comp, prop, value.get<int64_t>());
            break;
        case PropertyType::UInt8:
            if (value.is_number_integer())
                SetPropertyValue<uint8_t>(comp, prop, static_cast<uint8_t>(value.get<int32_t>()));
            break;
        case PropertyType::UInt16:
            if (value.is_number_integer())
                SetPropertyValue<uint16_t>(comp, prop, static_cast<uint16_t>(value.get<int32_t>()));
            break;
        case PropertyType::UInt32:
            if (value.is_number_unsigned())
                SetPropertyValue<uint32_t>(comp, prop, value.get<uint32_t>());
            break;
        case PropertyType::UInt64:
            if (value.is_number_unsigned())
                SetPropertyValue<uint64_t>(comp, prop, value.get<uint64_t>());
            break;
        case PropertyType::Float:
            if (value.is_number())
                SetPropertyValue<float>(comp, prop, value.get<float>());
            break;
        case PropertyType::Double:
            if (value.is_number())
                SetPropertyValue<double>(comp, prop, value.get<double>());
            break;
        case PropertyType::Bool:
            if (value.is_boolean())
                SetPropertyValue<bool>(comp, prop, value.get<bool>());
            break;
        case PropertyType::String:
            if (value.is_string())
                SetPropertyValue<std::string>(comp, prop, value.get<std::string>());
            break;
        case PropertyType::Color:
            // "#RRGGBBAA" is what is written now; the packed number every file
            // before this used still reads. A malformed string keeps the
            // field's default rather than silently turning it black.
            if (value.is_string())
            {
                Deki::Color parsed;
                if (ColorFromHex(value.get<std::string>().c_str(), parsed))
                    GetPropertyRef<Deki::Color>(comp, prop) = parsed;
                else
                    DEKI_LOG_WARNING("Scene: '%s' is not a colour like #RRGGBBAA on property '%s'",
                                     value.get<std::string>().c_str(), prop.name);
            }
            else if (value.is_number_unsigned())
                GetPropertyRef<Deki::Color>(comp, prop) = Deki::Color::FromRGBA8888(value.get<uint32_t>());
            else if (value.is_number_integer())
                GetPropertyRef<Deki::Color>(comp, prop) = Deki::Color::FromRGBA8888(static_cast<uint32_t>(value.get<int64_t>()));
            break;
        case PropertyType::Enum:
        {
            // Both forms are accepted: the NAME, which is what the editor writes
            // now, and a bare number, which every file written before that used
            // and which stays readable. An unknown name leaves the field at its
            // default rather than guessing, since guessing is what made this
            // worth changing.
            bool haveValue = false;
            int32_t intVal = 0;
            if (value.is_string())
            {
                const std::string name = value.get<std::string>();
                haveValue = EnumValueForName(prop, name.c_str(), intVal);
                if (!haveValue)
                    DEKI_LOG_WARNING("Scene: '%s' is not a value of enum property '%s'; leaving the default",
                                     name.c_str(), prop.name);
            }
            else if (value.is_number_integer())
            {
                intVal = value.get<int32_t>();
                haveValue = true;
            }
            if (haveValue)
            {
                // Use enumSize to write the correct number of bytes
                switch (prop.enumSize)
                {
                    case 1:
                        SetPropertyValue<uint8_t>(comp, prop, static_cast<uint8_t>(intVal));
                        break;
                    case 2:
                        SetPropertyValue<uint16_t>(comp, prop, static_cast<uint16_t>(intVal));
                        break;
                    default:
                        SetPropertyValue<int32_t>(comp, prop, intVal);
                        break;
                }
            }
            break;
        }
        case PropertyType::Asset:
            // Asset paths are stored as strings
            if (value.is_string())
                SetPropertyValue<std::string>(comp, prop, value.get<std::string>());
            break;
        case PropertyType::ObjectRef:
        {
            ObjectRefBase& ref = GetPropertyRef<ObjectRefBase>(comp, prop);
            if (value.is_object())
            {
                ref.id = value.value("id", 0u);
                ref.componentTypeId = LegacyComponentTypeId(value.value("typeId", 0u));
            }
            else if (value.is_number_unsigned() || value.is_number_integer())
            {
                ref.id = value.get<uint32_t>();
                ref.componentTypeId = 0;
            }
            ref.resolved = nullptr;
            break;
        }
        case PropertyType::Array:
            // Deserialize array based on element type
            if (value.is_array())
            {
                void* ptr = reinterpret_cast<char*>(comp) + prop.offset;

                switch (prop.elementType)
                {
                    case PropertyType::String:
                    {
                        auto& vec = *reinterpret_cast<std::vector<std::string>*>(ptr);
                        vec.clear();
                        for (const auto& item : value)
                        {
                            if (item.is_string())
                                vec.push_back(item.get<std::string>());
                        }
                        break;
                    }
                    case PropertyType::Int32:
                    {
                        auto& vec = *reinterpret_cast<std::vector<int32_t>*>(ptr);
                        vec.clear();
                        for (const auto& item : value)
                        {
                            if (item.is_number_integer())
                                vec.push_back(item.get<int32_t>());
                        }
                        break;
                    }
                    case PropertyType::Float:
                    {
                        auto& vec = *reinterpret_cast<std::vector<float>*>(ptr);
                        vec.clear();
                        for (const auto& item : value)
                        {
                            if (item.is_number())
                                vec.push_back(item.get<float>());
                        }
                        break;
                    }
                    case PropertyType::Bool:
                    {
                        auto& vec = *reinterpret_cast<std::vector<bool>*>(ptr);
                        vec.clear();
                        for (const auto& item : value)
                        {
                            if (item.is_boolean())
                                vec.push_back(item.get<bool>());
                        }
                        break;
                    }
                    case PropertyType::ObjectRef:
                    {
                        auto& vec = *reinterpret_cast<std::vector<ObjectRefBase>*>(ptr);
                        vec.clear();
                        for (const auto& item : value)
                        {
                            ObjectRefBase ref;
                            ref.resolved = nullptr;
                            if (item.is_object())
                            {
                                ref.id = item.value("id", 0u);
                                ref.componentTypeId = LegacyComponentTypeId(item.value("typeId", 0u));
                            }
                            else if (item.is_number_unsigned() || item.is_number_integer())
                            {
                                ref.id = item.get<uint32_t>();
                                ref.componentTypeId = 0;
                            }
                            vec.push_back(ref);
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
            break;
        case PropertyType::AssetRef:
        {
            // AssetRef stores GUID in the guid field, and optionally source for derived assets
            Deki::AssetRefBase& ref = GetPropertyRef<Deki::AssetRefBase>(comp, prop);
            if (value.is_string())
            {
                // Backward compatible: simple string (no source)
                ref.guid = value.get<std::string>();
                ref.source.clear();
            }
            else if (value.is_object())
            {
                // Object with guid and source
                ref.guid = value.value("guid", "");
                ref.source = value.value("source", "");
            }
            ref.ptr = nullptr;  // Will be loaded on play
            ref.loadAttempted = false;
            break;
        }
        case PropertyType::Vector2:
            if (value.is_array() && value.size() >= 2)
            {
                Vector2* ptr = reinterpret_cast<Vector2*>(
                    reinterpret_cast<char*>(comp) + prop.offset);
                ptr->x = value[0].get<float>();
                ptr->y = value[1].get<float>();
            }
            break;
        case PropertyType::Vector3:
            if (value.is_array() && value.size() >= 3)
            {
                Vector3* ptr = reinterpret_cast<Vector3*>(
                    reinterpret_cast<char*>(comp) + prop.offset);
                ptr->x = value[0].get<float>();
                ptr->y = value[1].get<float>();
                ptr->z = value[2].get<float>();
            }
            break;
        default:
            break;
    }
}

// =============================================================================
// Scene Instance support
// =============================================================================

std::function<std::string(const std::string& guid)> Scene::s_GuidToPathResolver;

// Static set to detect circular instance references during expansion
static thread_local std::set<std::string> s_ExpandingGuids;

bool Scene::ExpandInstance(Object* instanceObj)
{
    if (!instanceObj || !instanceObj->IsSceneInstance())
        return false;

    const std::string& guid = instanceObj->GetSourceSceneGuid();

    // Circular reference detection
    if (s_ExpandingGuids.count(guid))
    {
        DEKI_LOG_ERROR("Scene::ExpandInstance: Circular reference detected for GUID '%s'", guid.c_str());
        return false;
    }

    if (!s_GuidToPathResolver)
    {
        DEKI_LOG_ERROR("Scene::ExpandInstance: No GUID resolver set");
        return false;
    }

    std::string sourcePath = s_GuidToPathResolver(guid);
    if (sourcePath.empty())
    {
        DEKI_LOG_ERROR("Scene::ExpandInstance: Could not resolve GUID '%s' to path", guid.c_str());
        return false;
    }

    // Load source scene
    s_ExpandingGuids.insert(guid);

    Scene sourceScene;
    bool loaded = sourceScene.LoadFromFile(sourcePath);

    if (!loaded)
    {
        s_ExpandingGuids.erase(guid);
        DEKI_LOG_ERROR("Scene::ExpandInstance: Failed to load source scene '%s'", sourcePath.c_str());
        return false;
    }

    // Determine which objects to clone from source scene
    // Single-root: skip root wrapper, clone its children directly
    // but copy root's components onto the instance object
    auto& sourceRoots = sourceScene.GetObjects();
    bool isSingleRoot = (sourceRoots.size() == 1);
    std::vector<Object*> objectsToClone;

    if (isSingleRoot)
    {
        // Use the source scene's root name for the instance
        instanceObj->SetName(sourceRoots[0]->GetName());

        // Copy root's components onto the instance object so they aren't lost
        nlohmann::json rootJson = sourceScene.SaveObjectToJson(sourceRoots[0]);
        if (rootJson.contains("components"))
        {
            auto& registry = ComponentRegistry::Instance();
            for (const auto& compJson : rootJson["components"])
            {
                std::string typeName = compJson.value("type", "");
                std::string classHint = compJson.value("class", "");
                Component* comp = Internal::AddComponentByName(*instanceObj, typeName, classHint);
                if (comp)
                {
                    const ComponentMeta* meta = registry.GetMeta(comp->GetType());  // not typeName: a stale GUID resolved by class name
                    if (meta && compJson.contains("properties"))
                    {
                        if (meta->baseTypeId != 0)
                        {
                            const ComponentMeta* baseMeta = registry.GetMeta(meta->baseTypeId);
                            if (baseMeta && baseMeta->propertyCount > 0)
                                for (int i = 0; i < baseMeta->propertyCount; ++i)
                                    LoadPropertyFromJson(comp, baseMeta->properties[i], compJson["properties"]);
                        }
                        for (int i = 0; i < meta->propertyCount; ++i)
                            LoadPropertyFromJson(comp, meta->properties[i], compJson["properties"]);
                    }
                }
                else if (!typeName.empty() && typeName != "Unknown")
                {
                    auto* missing = new MissingComponent();
                    missing->m_TypeName = typeName;
                    missing->m_ClassName = classHint;
                    missing->m_OriginalTypeId = HashString(typeName.c_str());
                    if (compJson.contains("properties"))
                        missing->m_JsonProperties = compJson["properties"];
                    Internal::AttachComponent(*instanceObj, missing);
                }
            }
        }

        for (auto* child : sourceRoots[0]->GetChildren())
            objectsToClone.push_back(child);
    }
    else
    {
        for (auto* root : sourceRoots)
            objectsToClone.push_back(root);
    }

    // Accumulated ID remap across all cloned children (needed for instance root ObjectRefs)
    std::unordered_map<uint32_t, uint32_t> idRemap;

    // Single-root: the source root collapses into the instance object itself
    // (its components were copied above, its children are cloned below). Any
    // ObjectRef inside the source pointing to the source root must remap to
    // the instance object's id.
    if (isSingleRoot)
        idRemap[sourceRoots[0]->GetId()] = instanceObj->GetId();

    // Clone source objects: serialize to JSON, then deserialize as child
    // Every id already in use in this scene, so the deterministic ids assigned
    // to the clones below never collide. Collected ONCE here.
    std::unordered_set<uint32_t> usedIds;
    for (auto* root : GetObjects())
        VisitSubtree(root, [&usedIds](Object* o)
                     { usedIds.insert(o->GetId()); });

    for (auto* sourceObj : objectsToClone)
    {
        nlohmann::json objJson = sourceScene.SaveObjectToJson(sourceObj);
        Object* cloned = LoadObjectFromJson(objJson);
        if (cloned)
        {
            // Remove from root objects list (LoadObjectFromJson adds it there)
            RemoveObject(cloned);
            cloned->SetOwnerScene(this);
            instanceObj->AddChild(cloned);
#ifdef DEKI_TRANSFORM_2D
            cloned->SetLocalPosition(cloned->GetX(), cloned->GetY());
#endif

            // Mark all cloned objects as part of instance
            VisitSubtree(cloned, [](Object* o)
                         { o->SetPartOfInstance(true); });

            // Remap object IDs to avoid duplicates across instances
            // Use deterministic IDs so external ObjectRefs pointing into this
            // instance remain valid across save/load (hot reload, play mode).
            // usedIds was collected once before this loop (it used to be
            // rebuilt from the whole scene for every cloned root).
            const uint32_t instanceRootId = instanceObj->GetId();
            VisitSubtree(cloned, [&](Object* obj)
                         {
                uint32_t oldId = obj->GetId();
                uint32_t newId = Object::GenerateDeterministicId(instanceRootId, oldId);
                uint32_t salt = 0;
                while (usedIds.count(newId))
                {
                    newId = Object::GenerateDeterministicId(newId, ++salt);
                }
                usedIds.insert(newId);
                idRemap[oldId] = newId;
                obj->SetId(newId); });

            // Update ObjectRef fields to point to remapped IDs
            VisitSubtree(cloned, [&idRemap](Object* obj)
                         {
                for (auto* comp : obj->GetComponents())
                    comp->RemapObjectRefs(idRemap); });

            // Resolve ObjectRefs so .resolved pointers are set immediately
            // Don't MarkRefsResolved() - let ProcessInternalAwake() handle that
            // so it also resolves AssetRefs (sprite textures, etc.)
            VisitSubtree(cloned, [this](Object* obj)
                         {
                for (auto* comp : obj->GetComponents())
                    comp->ResolveObjectRefs(this); });
        }
    }

    // Remap and resolve ObjectRefs on instance object's own components
    // (copied from single-root source; their ObjectRefs reference child IDs that were remapped)
    // Don't MarkRefsResolved() - let ProcessInternalAwake() resolve AssetRefs too
    if (isSingleRoot && !instanceObj->GetComponents().empty())
    {
        for (auto* comp : instanceObj->GetComponents())
        {
            comp->RemapObjectRefs(idRemap);
            comp->ResolveObjectRefs(this);
        }
    }

    // Apply overrides if any
    const std::string& overridesStr = instanceObj->GetInstanceOverridesJson();
    if (!overridesStr.empty())
    {
        try
        {
            nlohmann::json overrides = nlohmann::json::parse(overridesStr);
            ApplyOverrides(instanceObj, overrides);
        }
        catch (const std::exception& e)
        {
            DEKI_LOG_ERROR("Scene::ExpandInstance: Failed to parse overrides: %s", e.what());
        }
    }

    // Reset resolution state on all expanded components so ProcessInternalAwake()
    // resolves AssetRefs (sprite textures, fonts, etc.) on the next frame.
    // This mirrors InvalidateAllAssets() which is what makes alt-tab fix rendering.
    VisitSubtree(instanceObj, [](Object* obj)
                 {
        for (auto* comp : obj->GetComponents())
            comp->ResetRefsResolved(); });

    m_ComponentsCacheDirty = true;
    s_ExpandingGuids.erase(guid);
    return true;
}

void Scene::CollapseInstance(Object* instanceObj)
{
    if (!instanceObj) return;

    // Delete all children (they are expanded from source)
    auto children = instanceObj->GetChildren();  // copy
    for (auto* child : children)
    {
        instanceObj->RemoveChild(child);
        delete child;
    }

    m_ComponentsCacheDirty = true;
}

void Scene::ApplyOverrides(Object* instanceObj, const nlohmann::json& overrides)
{
    if (!overrides.is_array()) return;

    auto& registry = ComponentRegistry::Instance();

    for (const auto& override : overrides)
    {
        std::string objectPath = override.value("objectPath", "");

        // Handle object-level property overrides (e.g., active state)
        if (override.contains("objectProperty"))
        {
            std::string objProp = override["objectProperty"].get<std::string>();
            Object* target = FindObjectByPath(instanceObj, objectPath);
            if (!target) continue;

            if (objProp == "active" && override.contains("value"))
            {
                target->SetActive(override["value"].get<bool>());
            }
            continue;
        }

        std::string componentType = override.value("component", "");
        std::string propertyName = override.value("property", "");

        if (componentType.empty() || propertyName.empty()) continue;

        // Find target object
        Object* target = FindObjectByPath(instanceObj, objectPath);
        if (!target) continue;

        // Find component
        const ComponentMeta* meta = registry.GetMeta(componentType);
        if (!meta) continue;

        Component* comp = target->GetComponentByTypeId(meta->typeId);
        if (!comp) continue;

        // Find property and apply value
        auto findAndApply = [&](const PropertyInfo* props, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                if (std::string(props[i].name) == propertyName && override.contains("value"))
                {
                    nlohmann::json tempProps;
                    tempProps[propertyName] = override["value"];
                    LoadPropertyFromJson(comp, props[i], tempProps);
                    return true;
                }
            }
            return false;
        };

        // Check base class properties first
        if (meta->baseTypeId != 0)
        {
            const ComponentMeta* baseMeta = registry.GetMeta(meta->baseTypeId);
            if (baseMeta && baseMeta->propertyCount > 0)
            {
                if (findAndApply(baseMeta->properties, baseMeta->propertyCount))
                    continue;
            }
        }

        // Check derived class properties
        findAndApply(meta->properties, meta->propertyCount);
    }
}

Object* Scene::FindObjectByPath(Object* root, const std::string& path)
{
    if (path.empty()) return root;

    Object* current = root;
    size_t start = 0;

    while (start < path.size())
    {
        size_t end = path.find('/', start);
        std::string segment = (end == std::string::npos)
                                  ? path.substr(start)
                                  : path.substr(start, end - start);

        if (segment.empty())
        {
            start = (end == std::string::npos) ? path.size() : end + 1;
            continue;
        }

        Object* found = nullptr;
        for (auto* child : current->GetChildren())
        {
            if (child->GetName() == segment)
            {
                found = child;
                break;
            }
        }

        if (!found) return nullptr;
        current = found;
        start = (end == std::string::npos) ? path.size() : end + 1;
    }

    return current;
}

void Scene::CollectOverrides(const Object* instanceRoot, const Object* obj,
                             nlohmann::json& overrides) const
{
    if (!instanceRoot->IsSceneInstance() || !s_GuidToPathResolver)
        return;

    // Load source scene for comparison
    std::string sourcePath = s_GuidToPathResolver(instanceRoot->GetSourceSceneGuid());
    if (sourcePath.empty()) return;

    Scene sourceScene;
    if (!sourceScene.LoadFromFile(sourcePath)) return;

    auto& registry = ComponentRegistry::Instance();

    // Compare each child of the instance against the corresponding source object
    std::function<void(const Object*, const Object*)> diffObjects =
        [&](const Object* instanceObj, const Object* sourceObj)
    {
        if (!instanceObj || !sourceObj) return;

        std::string objectPath = BuildObjectPath(
            const_cast<Object*>(instanceObj),
            const_cast<Object*>(instanceRoot));

        // Compare components
        for (const auto* comp : instanceObj->GetComponents())
        {
            const ComponentMeta* meta = registry.GetMeta(comp->GetType());
            if (!meta) continue;

            // Find corresponding component in source
            const Component* sourceComp = sourceObj->GetComponentByTypeId(meta->typeId);
            if (!sourceComp) continue;

            // Compare each property
            auto compareProps = [&](const PropertyInfo* props, int count)
            {
                for (int i = 0; i < count; ++i)
                {
                    const PropertyInfo& prop = props[i];

                    // Skip ObjectRef properties — IDs are remapped per-instance
                    // and cannot be meaningfully compared or stored as overrides
                    if (prop.type == PropertyType::ObjectRef)
                        continue;
                    if (prop.type == PropertyType::Array && prop.elementType == PropertyType::ObjectRef)
                        continue;

                    // Serialize both values to JSON for comparison
                    nlohmann::json currentJson, sourceJson;
                    SavePropertyToJson(comp, prop, currentJson);
                    sourceScene.SavePropertyToJson(sourceComp, prop, sourceJson);

                    if (currentJson.contains(prop.name) && sourceJson.contains(prop.name) &&
                        currentJson[prop.name] != sourceJson[prop.name])
                    {
                        nlohmann::json override;
                        override["objectPath"] = objectPath;
                        override["component"] = meta->serializedName;
                        override["property"] = prop.name;
                        override["value"] = currentJson[prop.name];
                        override["sourceValue"] = sourceJson[prop.name];
                        overrides.push_back(override);
                    }
                }
            };

            // Check base properties
            if (meta->baseTypeId != 0)
            {
                const ComponentMeta* baseMeta = registry.GetMeta(meta->baseTypeId);
                if (baseMeta && baseMeta->propertyCount > 0)
                    compareProps(baseMeta->properties, baseMeta->propertyCount);
            }

            // Check derived properties
            compareProps(meta->properties, meta->propertyCount);
        }

        // Compare object-level active state
        if (instanceObj->IsActive() != sourceObj->IsActive())
        {
            nlohmann::json override;
            override["objectPath"] = objectPath;
            override["objectProperty"] = "active";
            override["value"] = instanceObj->IsActive();
            override["sourceValue"] = sourceObj->IsActive();
            overrides.push_back(override);
        }

        // Recurse into children (match by index since names could be duplicated)
        const auto& instanceChildren = instanceObj->GetChildren();
        const auto& sourceChildren = sourceObj->GetChildren();
        size_t minChildren = std::min(instanceChildren.size(), sourceChildren.size());
        for (size_t i = 0; i < minChildren; ++i)
        {
            diffObjects(instanceChildren[i], sourceChildren[i]);
        }
    };

    // Diff instance's children against source objects
    // Single-root: compare against root's children (ExpandInstance skips root wrapper)
    const auto& instanceChildren = instanceRoot->GetChildren();
    const auto& sourceRoots = sourceScene.GetObjects();

    const std::vector<Object*>& sourceCompareList =
        (sourceRoots.size() == 1) ? sourceRoots[0]->GetChildren() : sourceRoots;

    size_t minCount = std::min(instanceChildren.size(), sourceCompareList.size());
    for (size_t i = 0; i < minCount; ++i)
    {
        diffObjects(instanceChildren[i], sourceCompareList[i]);
    }

    // Single-root: also diff instance root's components against source root's components
    // (ExpandInstance copies root components onto instance object)
    if (sourceRoots.size() == 1)
    {
        diffObjects(instanceRoot, sourceRoots[0]);
    }
}

std::string Scene::BuildObjectPath(Object* obj, Object* instanceRoot)
{
    if (obj == instanceRoot) return "";

    std::string path;
    Object* current = obj;

    while (current && current != instanceRoot)
    {
        if (!path.empty())
            path = current->GetName() + "/" + path;
        else
            path = current->GetName();
        current = current->GetParent();
    }

    return path;
}

#endif  // DEKI_EDITOR

}  // namespace Deki
