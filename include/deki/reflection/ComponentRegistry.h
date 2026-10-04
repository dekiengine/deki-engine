#pragma once

#include <deki/reflection/Property.h>
#include <vector>
#include <string>

#ifdef DEKI_EDITOR
#include <unordered_map>
#include <unordered_set>
#endif

// DLL export/import in editor builds, so all DLLs share one ComponentRegistry.
#ifdef DEKI_EDITOR
#ifdef _WIN32
#ifdef DEKI_ENGINE_EXPORTS
#define COMPONENT_REGISTRY_API __declspec(dllexport)
#else
#define COMPONENT_REGISTRY_API __declspec(dllimport)
#endif
#else
#define COMPONENT_REGISTRY_API
#endif
#else
#define COMPONENT_REGISTRY_API
#endif

// Registry of component metadata.
//
// On a device it looks metadata up by type id. In the editor it also looks up
// by name and lists components.

namespace Deki
{

class COMPONENT_REGISTRY_API ComponentRegistry
{
public:
    static ComponentRegistry& Instance();

    /// Registers a component's static metadata.
    void Register(const ComponentMeta* meta);

    /// Metadata for a type id, or nullptr when unknown.
    const ComponentMeta* GetMeta(uint32_t typeId) const;

    // ========================================================================
    // Compact field tables, on every platform including devices.
    //
    // ComponentMeta is editor-only (display names, tooltips, ranges). Devices
    // still need to find a field by name for data-driven systems, so codegen
    // also emits one flat FieldRef table per concrete component (own and
    // inherited fields) and registers it here. 8 bytes per field, in flash.
    // ========================================================================

    /// Registers a component's field table. Registering the same typeId again
    /// replaces the table, for hot reload.
    void RegisterFields(uint32_t typeId, const FieldRef* fields, int count);

    /// One field by name hash, or nullptr when absent.
    const FieldRef* FindField(uint32_t typeId, uint32_t nameHash) const;

    /// The whole table for a type, or nullptr and outCount 0 when absent.
    const FieldRef* GetFields(uint32_t typeId, int& outCount) const;

#ifdef DEKI_EDITOR
    /// Metadata for a serialized name from a scene file, or nullptr when
    /// unknown. Falls back to DEKI_FORMER_NAME.
    const ComponentMeta* GetMeta(const std::string& name) const;

    /// Metadata for a C++ class name (e.g. "PlatformSetupComponent"), or nullptr.
    const ComponentMeta* GetMetaByClassName(const std::string& className) const;

    /// Metadata for a type id that is the hash of a name a component used to
    /// have, or nullptr when no component ever had that name.
    ///
    /// Scenes store some component references as a bare type id with no name
    /// (ObjectRef::componentTypeId, PropertyRef::componentTypeId). That id is
    /// the hash of the class name at save time, and a hash cannot be turned
    /// back into a name, so GetMeta(name) cannot resolve it. This looks up
    /// HashString(formerName) instead. Callers rewrite the reference to the
    /// current id, so the file is migrated on its next save.
    const ComponentMeta* GetMetaByFormerTypeId(uint32_t formerTypeId) const;

    /// Creates a component by type id. Returns nullptr when the id is unknown
    /// or the class is abstract.
    Component* CreateComponent(uint32_t typeId) const;

    /// Creates a component by serialized name. Returns nullptr when the name is
    /// unknown or the class is abstract.
    Component* CreateComponent(const std::string& name) const;

    const std::vector<const ComponentMeta*>& GetAllComponents() const;

    /// Components in a category (e.g. "2D").
    std::vector<const ComponentMeta*> GetComponentsByCategory(const std::string& category) const;

    /// Every category name, each once.
    std::vector<std::string> GetCategories() const;

    /// Registers a plugin component. Use it instead of Register() for plugins,
    /// so hot-reload can clear them and keep the engine components.
    void RegisterPluginComponent(const ComponentMeta* meta);

    /// Removes the components registered with RegisterPluginComponent(). Called
    /// before plugins hot-reload.
    void ClearPluginComponents();

    /// True when the component was registered with RegisterPluginComponent().
    bool IsPluginComponent(uint32_t typeId) const;

    /// Removes a component. Package hot-reload calls it before FreeLibrary, so
    /// the registry holds no pointers into the unloaded DLL.
    void Unregister(uint32_t typeId);

    /// Marks the current entries as engine-core. Call once after engine init,
    /// before loading any packages. ClearNonEngineComponents() keeps them.
    void FreezeEngineComponents();

    /// Removes every package and plugin component in one step, so no stale
    /// pointer survives FreeLibrary.
    void ClearNonEngineComponents();
#endif

private:
    ComponentRegistry() = default;
    ComponentRegistry(const ComponentRegistry&) = delete;
    ComponentRegistry& operator=(const ComponentRegistry&) = delete;

    std::vector<const ComponentMeta*> m_Components;

#ifndef DEKI_EDITOR
    // typeId -> meta, sorted by typeId so GetMeta can binary search. GetMeta
    // runs for every component on scene load. A sorted vector is one block
    // with no per-entry allocation, which an MCU needs, and gives O(log n).
    //
    // Device builds only: the editor has m_ByType, and it unloads packages,
    // which this index would have to follow.
    struct TypeIndexEntry
    {
        uint32_t typeId;
        const ComponentMeta* meta;
    };
    std::vector<TypeIndexEntry> m_TypeIndex;

    void IndexInsertOrReplace(const ComponentMeta* meta);
#endif

#ifdef DEKI_EDITOR
    /// Adds this component's DEKI_FORMER_NAME entries to the alias index. When
    /// two components claim the same former name, the first keeps it and an
    /// error is logged.
    void IndexFormerNames(const ComponentMeta* meta);

    /// Rebuilds the alias index from the components still registered. Used
    /// after an unload, so the index does not point into an unmapped DLL.
    void RebuildFormerNameIndex();
#endif

    // Field tables exist on all platforms (see RegisterFields). The pointers are
    // static storage in the owning package's .gen.cpp, so in the editor they
    // follow the same freeze/restore lifecycle as m_Components when a package
    // DLL unloads.
    struct FieldTable
    {
        uint32_t typeId;
        const FieldRef* fields;
        int count;
    };
    std::vector<FieldTable> m_FieldTables;

#ifdef DEKI_EDITOR
    std::vector<FieldTable> m_FrozenFieldTables;
    std::unordered_map<uint32_t, const ComponentMeta*> m_ByType;
    std::unordered_map<std::string, const ComponentMeta*> m_ByName;
    // DEKI_FORMER_NAME -> the component that now has it, so a scene saved
    // before a rename still resolves. Separate from m_ByName because a current
    // name must always win over another component's former name.
    std::unordered_map<std::string, const ComponentMeta*> m_ByFormerName;
    // The same table keyed by HashString(formerName), for references that
    // store a type id and no name. See GetMetaByFormerTypeId.
    std::unordered_map<uint32_t, const ComponentMeta*> m_ByFormerTypeId;
    std::unordered_set<uint32_t> m_PluginTypeIds;
    std::unordered_map<uint32_t, std::string>
        m_PluginTypeToName;  // typeId → serializedName for dereference-safe cleanup
    std::unordered_set<uint32_t> m_EngineTypeIds;

    // Engine-core state captured by FreezeEngineComponents.
    // ClearNonEngineComponents restores from these, so it never dereferences
    // pointers into unloaded package or plugin DLLs.
    std::vector<const ComponentMeta*> m_FrozenComponents;
    std::unordered_map<uint32_t, const ComponentMeta*> m_FrozenByType;
    std::unordered_map<std::string, const ComponentMeta*> m_FrozenByName;
    std::unordered_map<std::string, const ComponentMeta*> m_FrozenByFormerName;
#endif
};

}  // namespace Deki
