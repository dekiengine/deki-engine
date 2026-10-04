#include <deki/reflection/SettingsRegistry.h>
#include <deki/LogSystem.h>
#include <cstring>

#ifdef DEKI_EDITOR
#include <nlohmann/json.hpp>
#endif

namespace Deki
{

SettingsRegistry& SettingsRegistry::Instance()
{
    static SettingsRegistry s_Instance;
    return s_Instance;
}

void SettingsRegistry::Register(const char* sectionName, const ComponentMeta* meta)
{
    if (!sectionName || !meta || !meta->createFunc)
    {
        return;
    }

#ifdef DEKI_EDITOR
    // Duplicate-section policy: log error + ignore second registration.
    // ComponentRegistry silently overwrites, but for settings we want to surface
    // the conflict so a malformed package doesn't quietly stomp another's section.
    auto existing = m_ByName.find(sectionName);
    if (existing != m_ByName.end())
    {
        DEKI_LOG(LogLevel::Error,
                 "[SettingsRegistry] Duplicate section '%s' (existing typeId=%u, new typeId=%u) - ignoring second "
                 "registration",
                 sectionName, m_All[existing->second].meta->typeId, meta->typeId);
        return;
    }
#endif

    SettingsTypeInfo info;
    info.sectionName = sectionName;
    info.meta = meta;
    info.instance = meta->createFunc();
    info.isPlugin = false;

    size_t idx = m_All.size();
    m_All.push_back(info);

#ifdef DEKI_EDITOR
    m_ByName[sectionName] = idx;
    m_ByTypeId[meta->typeId] = idx;
    m_EngineTypeIds.insert(meta->typeId);

    DEKI_LOG(LogLevel::Debug, "[SettingsRegistry] Registered '%s' (%d properties)", sectionName, meta->propertyCount);
#endif
}

void SettingsRegistry::RegisterPlugin(const char* sectionName, const ComponentMeta* meta)
{
    if (!sectionName || !meta || !meta->createFunc)
    {
        return;
    }

#ifdef DEKI_EDITOR
    auto existing = m_ByName.find(sectionName);
    if (existing != m_ByName.end())
    {
        DEKI_LOG(LogLevel::Error,
                 "[SettingsRegistry] Duplicate plugin section '%s' (existing typeId=%u, new typeId=%u) - ignoring "
                 "second registration",
                 sectionName, m_All[existing->second].meta->typeId, meta->typeId);
        return;
    }
#endif

    SettingsTypeInfo info;
    info.sectionName = sectionName;
    info.meta = meta;
    info.instance = meta->createFunc();
    info.isPlugin = true;

    size_t idx = m_All.size();
    m_All.push_back(info);

#ifdef DEKI_EDITOR
    m_ByName[sectionName] = idx;
    m_ByTypeId[meta->typeId] = idx;
    m_PluginTypeIds.insert(meta->typeId);

    DEKI_LOG(LogLevel::Debug, "[SettingsRegistry] Registered plugin '%s' (%d properties)", sectionName,
             meta->propertyCount);
#endif
}

Component* SettingsRegistry::GetByName(const char* sectionName)
{
    if (!sectionName)
    {
        return nullptr;
    }
#ifdef DEKI_EDITOR
    auto it = m_ByName.find(sectionName);
    if (it != m_ByName.end())
    {
        return m_All[it->second].instance;
    }
#else
    for (auto& info : m_All)
    {
        if (info.sectionName && std::strcmp(info.sectionName, sectionName) == 0)
        {
            return info.instance;
        }
    }
#endif
    return nullptr;
}

Component* SettingsRegistry::GetByTypeId(uint32_t typeId)
{
#ifdef DEKI_EDITOR
    auto it = m_ByTypeId.find(typeId);
    if (it != m_ByTypeId.end())
    {
        return m_All[it->second].instance;
    }
#else
    for (auto& info : m_All)
    {
        if (info.meta && info.meta->typeId == typeId)
        {
            return info.instance;
        }
    }
#endif
    return nullptr;
}

#ifdef DEKI_EDITOR

void SettingsRegistry::ClearPluginSettings()
{
    // Delete plugin-owned instances (their vtables would dangle after FreeLibrary)
    // and rebuild from the engine-core baseline.
    for (auto& info : m_All)
    {
        if (info.isPlugin && info.instance)
        {
            delete info.instance;
            info.instance = nullptr;
        }
    }

    // Rebuild m_All / lookup maps from frozen engine-core snapshot if available;
    // otherwise filter out plugin entries in-place.
    if (!m_FrozenAll.empty())
    {
        m_All = m_FrozenAll;
        m_ByName = m_FrozenByName;
        m_ByTypeId = m_FrozenByTypeId;
    }
    else
    {
        std::vector<SettingsTypeInfo> kept;
        m_ByName.clear();
        m_ByTypeId.clear();
        for (auto& info : m_All)
        {
            if (!info.isPlugin)
            {
                size_t idx = kept.size();
                kept.push_back(info);
                if (info.sectionName)
                {
                    m_ByName[info.sectionName] = idx;
                }
                if (info.meta)
                {
                    m_ByTypeId[info.meta->typeId] = idx;
                }
            }
        }
        m_All = std::move(kept);
    }

    m_PluginTypeIds.clear();
}

void SettingsRegistry::Unregister(uint32_t typeId)
{
    auto it = m_ByTypeId.find(typeId);
    if (it == m_ByTypeId.end())
    {
        return;
    }

    size_t idx = it->second;
    auto& info = m_All[idx];

    // Delete the instance before FreeLibrary so its vtable resolves to live code.
    if (info.instance)
    {
        delete info.instance;
        info.instance = nullptr;
    }

    // Remove from name lookup
    if (info.sectionName)
    {
        m_ByName.erase(info.sectionName);
    }
    m_ByTypeId.erase(typeId);
    m_PluginTypeIds.erase(typeId);

    // Erase + rebuild index map (all elements after idx shift down by one).
    m_All.erase(m_All.begin() + idx);
    for (auto& [name, i] : m_ByName)
    {
        if (i > idx)
        {
            --i;
        }
    }
    for (auto& [tid, i] : m_ByTypeId)
    {
        if (i > idx)
        {
            --i;
        }
    }
}

bool SettingsRegistry::IsPluginSetting(uint32_t typeId) const
{
    return m_PluginTypeIds.count(typeId) > 0;
}

void SettingsRegistry::FreezeEngineSettings()
{
    m_FrozenAll = m_All;
    m_FrozenByName = m_ByName;
    m_FrozenByTypeId = m_ByTypeId;

    m_EngineTypeIds.clear();
    for (auto& info : m_All)
    {
        if (!info.isPlugin && info.meta)
        {
            m_EngineTypeIds.insert(info.meta->typeId);
        }
    }
}

namespace
{

// Mini per-property JSON serializer used by snapshot/restore. Mirrors the
// types Project.cpp's helpers handle. Settings types use a constrained
// property set (bool/int/float/string/enum) so this stays small.
void SaveProp(const Component* comp, const PropertyInfo& prop, nlohmann::json& out)
{
    switch (prop.type)
    {
        case PropertyType::Bool: out[prop.name] = GetPropertyValue<bool>(comp, prop); break;
        case PropertyType::Int8: out[prop.name] = (int32_t)GetPropertyValue<int8_t>(comp, prop); break;
        case PropertyType::Int16: out[prop.name] = (int32_t)GetPropertyValue<int16_t>(comp, prop); break;
        case PropertyType::Int32: out[prop.name] = GetPropertyValue<int32_t>(comp, prop); break;
        case PropertyType::UInt8: out[prop.name] = (int32_t)GetPropertyValue<uint8_t>(comp, prop); break;
        case PropertyType::UInt16: out[prop.name] = (int32_t)GetPropertyValue<uint16_t>(comp, prop); break;
        case PropertyType::UInt32: out[prop.name] = GetPropertyValue<uint32_t>(comp, prop); break;
        case PropertyType::Float: out[prop.name] = GetPropertyValue<float>(comp, prop); break;
        case PropertyType::String: out[prop.name] = GetPropertyValue<std::string>(comp, prop); break;
        case PropertyType::Enum:
        {
            // Settings files are authored and tracked in git like scenes, so
            // they store the enumerator's NAME for the same reason: a bare
            // number means whichever value sat at that position when the file
            // was written. A value the enum does not declare keeps the number.
            int32_t enumValue = 0;
            switch (prop.enumSize)
            {
                case 1: enumValue = (int32_t)GetPropertyValue<uint8_t>(comp, prop); break;
                case 2: enumValue = (int32_t)GetPropertyValue<uint16_t>(comp, prop); break;
                default: enumValue = GetPropertyValue<int32_t>(comp, prop); break;
            }
            if (const char* enumName = EnumNameForValue(prop, enumValue))
            {
                out[prop.name] = enumName;
            }
            else
            {
                out[prop.name] = enumValue;
            }
            break;
        }
        default: break;
    }
}

void LoadProp(Component* comp, const PropertyInfo& prop, const nlohmann::json& in)
{
    if (!in.contains(prop.name))
    {
        return;
    }
    const auto& v = in[prop.name];
    switch (prop.type)
    {
        case PropertyType::Bool:
            if (v.is_boolean())
            {
                SetPropertyValue<bool>(comp, prop, v.get<bool>());
            }
            break;
        case PropertyType::Int8:
            if (v.is_number_integer())
            {
                SetPropertyValue<int8_t>(comp, prop, (int8_t)v.get<int32_t>());
            }
            break;
        case PropertyType::Int16:
            if (v.is_number_integer())
            {
                SetPropertyValue<int16_t>(comp, prop, (int16_t)v.get<int32_t>());
            }
            break;
        case PropertyType::Int32:
            if (v.is_number_integer())
            {
                SetPropertyValue<int32_t>(comp, prop, v.get<int32_t>());
            }
            break;
        case PropertyType::UInt8:
            if (v.is_number_integer())
            {
                SetPropertyValue<uint8_t>(comp, prop, (uint8_t)v.get<int32_t>());
            }
            break;
        case PropertyType::UInt16:
            if (v.is_number_integer())
            {
                SetPropertyValue<uint16_t>(comp, prop, (uint16_t)v.get<int32_t>());
            }
            break;
        case PropertyType::UInt32:
            if (v.is_number_unsigned())
            {
                SetPropertyValue<uint32_t>(comp, prop, v.get<uint32_t>());
            }
            else if (v.is_number_integer())
            {
                SetPropertyValue<uint32_t>(comp, prop, (uint32_t)v.get<int32_t>());
            }
            break;
        case PropertyType::Float:
            if (v.is_number())
            {
                SetPropertyValue<float>(comp, prop, v.get<float>());
            }
            break;
        case PropertyType::String:
            if (v.is_string())
            {
                SetPropertyValue<std::string>(comp, prop, v.get<std::string>());
            }
            break;
        case PropertyType::Enum:
        {
            // Accepts the name written now and the bare number written before,
            // so a settings file from an older editor still loads.
            bool haveValue = false;
            int32_t intVal = 0;
            if (v.is_string())
            {
                haveValue = EnumValueForName(prop, v.get<std::string>().c_str(), intVal);
            }
            else if (v.is_number_integer())
            {
                intVal = v.get<int32_t>();
                haveValue = true;
            }
            if (haveValue)
            {
                switch (prop.enumSize)
                {
                    case 1: SetPropertyValue<uint8_t>(comp, prop, (uint8_t)intVal); break;
                    case 2: SetPropertyValue<uint16_t>(comp, prop, (uint16_t)intVal); break;
                    default: SetPropertyValue<int32_t>(comp, prop, intVal); break;
                }
            }
            break;
        }
        default: break;
    }
}

}  // namespace

std::string SettingsRegistry::SnapshotPluginSettingsAsJson() const
{
    nlohmann::json root = nlohmann::json::object();
    for (const auto& info : m_All)
    {
        if (!info.isPlugin || !info.instance || !info.meta)
        {
            continue;
        }
        nlohmann::json sectionJson = nlohmann::json::object();
        for (int i = 0; i < info.meta->propertyCount; ++i)
        {
            SaveProp(info.instance, info.meta->properties[i], sectionJson);
        }
        root[info.sectionName] = sectionJson;
    }
    return root.dump();
}

void SettingsRegistry::RestoreFromJson(const std::string& snapshotJson)
{
    if (snapshotJson.empty())
    {
        return;
    }
    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(snapshotJson);
    }
    catch (const std::exception& e)
    {
        DEKI_LOG(LogLevel::Warning, "[SettingsRegistry] Snapshot parse failed: %s", e.what());
        return;
    }
    if (!root.is_object())
    {
        return;
    }
    for (auto it = root.begin(); it != root.end(); ++it)
    {
        const std::string& section = it.key();
        const nlohmann::json& sectionJson = it.value();
        Component* inst = GetByName(section.c_str());
        if (!inst)
        {
            continue;  // package didn't reload yet - skip
        }
        // Use the meta we own; safer than going through ComponentRegistry,
        // which mirrors the same pointer.
        auto byNameIt = m_ByName.find(section);
        if (byNameIt == m_ByName.end())
        {
            continue;
        }
        const ComponentMeta* meta = m_All[byNameIt->second].meta;
        if (!meta)
        {
            continue;
        }
        for (int i = 0; i < meta->propertyCount; ++i)
        {
            LoadProp(inst, meta->properties[i], sectionJson);
        }
    }
}

#else  // !DEKI_EDITOR

void SettingsRegistry::ClearPluginSettings()
{ /* no-op */
}
void SettingsRegistry::Unregister(uint32_t /*typeId*/)
{ /* no-op */
}
bool SettingsRegistry::IsPluginSetting(uint32_t /*typeId*/) const
{
    return false;
}
void SettingsRegistry::FreezeEngineSettings()
{ /* no-op */
}
std::string SettingsRegistry::SnapshotPluginSettingsAsJson() const
{
    return {};
}
void SettingsRegistry::RestoreFromJson(const std::string& /*snapshotJson*/)
{ /* no-op */
}

#endif

}  // namespace Deki
