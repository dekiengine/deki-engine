#include <deki/reflection/ComponentRegistry.h>
#include <deki/LogSystem.h>
#include <deki/TypeId.h>  // HashString, for the former-name id index
#include <algorithm>
#include <cstdio>

namespace Deki
{

ComponentRegistry& ComponentRegistry::Instance()
{
    static ComponentRegistry s_Instance;
    return s_Instance;
}

#ifndef DEKI_EDITOR
void ComponentRegistry::IndexInsertOrReplace(const ComponentMeta* meta)
{
    auto it = std::lower_bound(m_TypeIndex.begin(), m_TypeIndex.end(), meta->typeId,
                               [](const TypeIndexEntry& e, uint32_t id) { return e.typeId < id; });

    if (it != m_TypeIndex.end() && it->typeId == meta->typeId)
    {
        it->meta = meta;
        return;
    }
    m_TypeIndex.insert(it, TypeIndexEntry{ meta->typeId, meta });
}
#endif

void ComponentRegistry::Register(const ComponentMeta* meta)
{
    if (!meta)
    {
        return;
    }

    // The indexed lookup answers "already registered?" without scanning
    // m_Components, which would make startup registration quadratic. The scan
    // is only for the rare hot-reload replacement.
    if (const ComponentMeta* existing = GetMeta(meta->typeId))
    {
        if (existing == meta)
        {
            return;  // Same pointer, skip
        }
        // A second registration of the same id is normally a hot reload: the
        // same class, recompiled, arriving from a rebuilt DLL. But a component's
        // id is the hash of its qualified name, so two DIFFERENT classes that
        // share a name collide here too, and replacing one with the other means
        // the survivor's field table is used to read the other's memory.
        //
        // The two are told apart by shape. A rebuild of the same class keeps its
        // size, base and property count; a different class sharing a name
        // almost never does. Complaining only on a shape change keeps an
        // ordinary reload silent while the dangerous case is named out loud,
        // rather than corrupting memory somewhere far from the cause.
        if (existing->instanceSize != meta->instanceSize || existing->baseTypeId != meta->baseTypeId ||
            existing->propertyCount != meta->propertyCount)
        {
            DEKI_LOG_ERROR("ComponentRegistry: two different components are registered as '%s'. Component "
                           "identity is the qualified class name, so this is a name clash, not a reload: "
                           "one is %u bytes with %d propertie(s), the other %u with %d. The one registered "
                           "second wins and the other's instances will be read with the wrong field "
                           "offsets. Rename one of them, or put them in different namespaces.",
                           meta->serializedName ? meta->serializedName : "?", (unsigned)existing->instanceSize,
                           existing->propertyCount, (unsigned)meta->instanceSize, meta->propertyCount);
        }
        for (size_t i = 0; i < m_Components.size(); ++i)
        {
            if (m_Components[i]->typeId == meta->typeId)
            {
                m_Components[i] = meta;  // Hot reload: replace old meta with new one
                break;
            }
        }
#ifdef DEKI_EDITOR
        m_ByType[meta->typeId] = meta;
        m_ByName[meta->serializedName] = meta;
        IndexFormerNames(meta);
#else
        IndexInsertOrReplace(meta);
#endif
        return;
    }

    m_Components.push_back(meta);

#ifdef DEKI_EDITOR
    m_ByType[meta->typeId] = meta;
    m_ByName[meta->serializedName] = meta;
    IndexFormerNames(meta);
#else
    IndexInsertOrReplace(meta);
#endif
}

#ifdef DEKI_EDITOR
void ComponentRegistry::IndexFormerNames(const ComponentMeta* meta)
{
    if (!meta || !meta->formerNames)
    {
        return;
    }
    for (int i = 0; i < meta->formerNameCount; ++i)
    {
        const char* former = meta->formerNames[i];
        if (!former || !*former)
        {
            continue;
        }
        // Two components claiming the same discarded name cannot both be
        // resolved, and picking one silently would load somebody's scene as the
        // wrong component. Keep the first and say so.
        auto existing = m_ByFormerName.find(former);
        if (existing != m_ByFormerName.end() && existing->second != meta)
        {
            DEKI_LOG_ERROR("ComponentRegistry: '%s' and '%s' both declare DEKI_FORMER_NAME(\"%s\"). A scene "
                           "written under that name cannot be resolved to either with confidence, so '%s' "
                           "keeps it. Only the class that actually had that name should claim it.",
                           existing->second->serializedName ? existing->second->serializedName : "?",
                           meta->serializedName ? meta->serializedName : "?", former,
                           existing->second->serializedName ? existing->second->serializedName : "?");
            continue;
        }
        m_ByFormerName[former] = meta;

        // Keyed by hash as well, for ObjectRef and PropertyRef, which store a
        // type id and no name. A current type id must win over a discarded
        // name's, so a former name that collides with some live component's id
        // is not indexed: that id already resolves, correctly, to that class.
        const uint32_t formerId = HashString(former);
        if (GetMeta(formerId) == nullptr)
        {
            m_ByFormerTypeId[formerId] = meta;
        }
    }
}
#endif

const ComponentMeta* ComponentRegistry::GetMeta(uint32_t typeId) const
{
#ifdef DEKI_EDITOR
    auto it = m_ByType.find(typeId);
    if (it != m_ByType.end())
    {
        return it->second;
    }
#else
    // Binary search the sorted type index: GetMeta runs per component per
    // object while a scene loads, and packages make the type count large.
    auto it = std::lower_bound(m_TypeIndex.begin(), m_TypeIndex.end(), typeId,
                               [](const TypeIndexEntry& e, uint32_t id) { return e.typeId < id; });
    if (it != m_TypeIndex.end() && it->typeId == typeId)
    {
        return it->meta;
    }
#endif
    return nullptr;
}

// ============================================================================
// Compact field tables (all platforms)
// ============================================================================

// m_FieldTables is kept sorted by typeId so GetFields is a binary search, for
// the same reason as GetMeta: it runs per component per object while a scene
// loads, and packages make the type count large.
void ComponentRegistry::RegisterFields(uint32_t typeId, const FieldRef* fields, int count)
{
    if (!fields || count <= 0)
    {
        return;
    }

    auto it = std::lower_bound(m_FieldTables.begin(), m_FieldTables.end(), typeId,
                               [](const FieldTable& t, uint32_t id) { return t.typeId < id; });
    if (it != m_FieldTables.end() && it->typeId == typeId)
    {
        // Hot reload: the package DLL was rebuilt, adopt the new table.
        it->fields = fields;
        it->count = count;
        return;
    }
    m_FieldTables.insert(it, { typeId, fields, count });
}

const FieldRef* ComponentRegistry::GetFields(uint32_t typeId, int& outCount) const
{
    auto it = std::lower_bound(m_FieldTables.begin(), m_FieldTables.end(), typeId,
                               [](const FieldTable& t, uint32_t id) { return t.typeId < id; });
    if (it != m_FieldTables.end() && it->typeId == typeId)
    {
        outCount = it->count;
        return it->fields;
    }
    outCount = 0;
    return nullptr;
}

const FieldRef* ComponentRegistry::FindField(uint32_t typeId, uint32_t nameHash) const
{
    int count = 0;
    const FieldRef* fields = GetFields(typeId, count);
    for (int i = 0; i < count; ++i)
    {
        if (fields[i].nameHash == nameHash)
        {
            return &fields[i];
        }
    }
    return nullptr;
}

#ifdef DEKI_EDITOR

const ComponentMeta* ComponentRegistry::GetMeta(const std::string& name) const
{
    auto it = m_ByName.find(name);
    if (it != m_ByName.end())
    {
        return it->second;
    }
    // Not a name any component answers to now, so try their former names
    // (DEKI_FORMER_NAME). This lets a class be renamed, or moved into a
    // namespace, without orphaning every instance in every saved scene. The
    // caller writes the current name back on save, so a file migrates the
    // first time it is opened and saved.
    auto former = m_ByFormerName.find(name);
    if (former != m_ByFormerName.end())
    {
        return former->second;
    }
    return nullptr;
}

Component* ComponentRegistry::CreateComponent(uint32_t typeId) const
{
    const auto* meta = GetMeta(typeId);
    if (meta && meta->createFunc && !meta->isAbstract)
    {
        return meta->createFunc();
    }
    return nullptr;
}

Component* ComponentRegistry::CreateComponent(const std::string& name) const
{
    const auto* meta = GetMeta(name);
    if (meta && meta->createFunc && !meta->isAbstract)
    {
        return meta->createFunc();
    }
    if (!meta)
    {
        DEKI_LOG(LogLevel::Warning, "CreateComponent('%s') -> meta NOT FOUND", name.c_str());
    }
    return nullptr;
}

const ComponentMeta* ComponentRegistry::GetMetaByClassName(const std::string& className) const
{
    for (const auto* meta : m_Components)
    {
        if (meta->name && className == meta->name)
        {
            return meta;
        }
    }
    return nullptr;
}

const std::vector<const ComponentMeta*>& ComponentRegistry::GetAllComponents() const
{
    return m_Components;
}

std::vector<const ComponentMeta*> ComponentRegistry::GetComponentsByCategory(const std::string& category) const
{
    std::vector<const ComponentMeta*> result;
    for (const auto* meta : m_Components)
    {
        if (meta->category && category == meta->category)
        {
            result.push_back(meta);
        }
    }
    return result;
}

std::vector<std::string> ComponentRegistry::GetCategories() const
{
    std::vector<std::string> categories;
    for (const auto* meta : m_Components)
    {
        if (meta->category)
        {
            std::string cat = meta->category;
            if (std::find(categories.begin(), categories.end(), cat) == categories.end())
            {
                categories.push_back(cat);
            }
        }
    }
    std::sort(categories.begin(), categories.end());
    return categories;
}

void ComponentRegistry::RegisterPluginComponent(const ComponentMeta* meta)
{
    Register(meta);
    if (meta)
    {
        m_PluginTypeIds.insert(meta->typeId);
        if (meta->serializedName)
        {
            m_PluginTypeToName[meta->typeId] = meta->serializedName;
        }
    }
}

void ComponentRegistry::ClearPluginComponents()
{
    // Remove plugin components using stored typeIds and names - avoids
    // dereferencing ComponentMeta* pointers that may be stale if the
    // plugin DLL was already unloaded.
    for (uint32_t typeId : m_PluginTypeIds)
    {
        m_ByType.erase(typeId);
        auto nameIt = m_PluginTypeToName.find(typeId);
        if (nameIt != m_PluginTypeToName.end())
        {
            m_ByName.erase(nameIt->second);
        }
    }

    // Rebuild m_Components from m_ByType (avoids pointer dereferences)
    m_Components.clear();
    for (const auto& [typeId, meta] : m_ByType)
    {
        m_Components.push_back(meta);
    }

    // The alias index points at metas too, and the plugin's are gone. Rebuilt
    // from the survivors, which are by definition still loaded.
    RebuildFormerNameIndex();

    m_PluginTypeIds.clear();
    m_PluginTypeToName.clear();
}

void ComponentRegistry::RebuildFormerNameIndex()
{
    m_ByFormerName.clear();
    m_ByFormerTypeId.clear();
    for (const ComponentMeta* meta : m_Components)
    {
        IndexFormerNames(meta);
    }
}

const ComponentMeta* ComponentRegistry::GetMetaByFormerTypeId(uint32_t formerTypeId) const
{
    if (formerTypeId == 0)
    {
        return nullptr;
    }
    auto it = m_ByFormerTypeId.find(formerTypeId);
    return it != m_ByFormerTypeId.end() ? it->second : nullptr;
}

bool ComponentRegistry::IsPluginComponent(uint32_t typeId) const
{
    return m_PluginTypeIds.count(typeId) > 0;
}

void ComponentRegistry::Unregister(uint32_t typeId)
{
    bool rebuildAliases = false;
    auto it = m_Components.begin();
    while (it != m_Components.end())
    {
        if ((*it)->typeId == typeId)
        {
            m_ByType.erase(typeId);
            if ((*it)->serializedName)
            {
                m_ByName.erase((*it)->serializedName);
            }
            it = m_Components.erase(it);
            rebuildAliases = true;
        }
        else
        {
            ++it;
        }
    }
    // The alias index may have pointed at the meta just removed.
    if (rebuildAliases)
    {
        RebuildFormerNameIndex();
    }
}

void ComponentRegistry::FreezeEngineComponents()
{
    m_EngineTypeIds.clear();
    for (const auto* meta : m_Components)
    {
        m_EngineTypeIds.insert(meta->typeId);
    }

    // Snapshot the entire engine-core state so ClearNonEngineComponents can
    // restore from it without dereferencing any pointers (which may be stale
    // if package/plugin DLLs were unloaded between freeze and clear).
    m_FrozenComponents = m_Components;
    m_FrozenByType = m_ByType;
    m_FrozenByName = m_ByName;
    m_FrozenByFormerName = m_ByFormerName;
    m_FrozenFieldTables = m_FieldTables;
}

void ComponentRegistry::ClearNonEngineComponents()
{
    // Restore from the frozen snapshot captured at FreezeEngineComponents().
    // This avoids iterating m_Components and dereferencing ComponentMeta*
    // pointers that may point into already-unloaded DLLs.
    m_Components = m_FrozenComponents;
    m_ByType = m_FrozenByType;
    m_ByName = m_FrozenByName;
    m_ByFormerName = m_FrozenByFormerName;
    // Field tables point into package .gen.cpp statics, so they die with the DLL
    // exactly like the metas do and are restored from the same snapshot.
    m_FieldTables = m_FrozenFieldTables;
    m_PluginTypeIds.clear();
}

#endif  // DEKI_EDITOR

}  // namespace Deki
