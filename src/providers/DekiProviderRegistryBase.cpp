#include "DekiProviderRegistryBase.h"
#include <deki/LogSystem.h>

#include <cstring>
#include <unordered_map>
#include <string>

namespace Deki
{

namespace
{

// One singleton per category string. Lives in deki-engine-core.dll so package
// DLLs all observe the same instance via dllimport.
std::unordered_map<std::string, ProviderRegistryBase*>& RegistryMap()
{
    static std::unordered_map<std::string, ProviderRegistryBase*> s_Map;
    return s_Map;
}

}  // namespace

ProviderRegistryBase& ProviderRegistryBase::ForCategory(const char* category)
{
    auto& map = RegistryMap();
    std::string key = category ? category : "";
    auto it = map.find(key);
    if (it != map.end())
    {
        return *it->second;
    }

    // Leaked at process exit on purpose, like the engine's other registries.
    auto* fresh = new ProviderRegistryBase();
    map.emplace(std::move(key), fresh);
    return *fresh;
}

void ProviderRegistryBase::Register(IPackage* provider, const char* providerId, bool isPlugin)
{
    if (!provider || !providerId)
    {
        DEKI_LOG_ERROR("[deki-provider-registry] Register called with null %s", !provider ? "provider" : "providerId");
        return;
    }

    // An entry with the same providerId is replaced, not duplicated, so a
    // package that re-registers after a hot reload has one entry.
    for (auto& e : m_Entries)
    {
        if (std::strcmp(e.providerId, providerId) == 0)
        {
            if (e.isPlugin && e.provider && e.provider != provider)
            {
                delete e.provider;
            }
            e.provider = provider;
            e.providerId = providerId;
            e.isPlugin = isPlugin;
            return;
        }
    }

    m_Entries.push_back(Entry{ provider, providerId, isPlugin });
}

void ProviderRegistryBase::Unregister(IPackage* provider)
{
    for (auto it = m_Entries.begin(); it != m_Entries.end(); ++it)
    {
        if (it->provider == provider)
        {
            if (it->isPlugin && it->provider)
            {
                delete it->provider;
            }
            m_Entries.erase(it);
            return;
        }
    }
}

IPackage* ProviderRegistryBase::GetById(const char* providerId) const
{
    if (!providerId)
    {
        return nullptr;
    }
    for (const auto& e : m_Entries)
    {
        if (std::strcmp(e.providerId, providerId) == 0)
        {
            return e.provider;
        }
    }
    return nullptr;
}

IPackage* ProviderRegistryBase::GetDefault() const
{
    return m_Entries.empty() ? nullptr : m_Entries.front().provider;
}

int ProviderRegistryBase::GetCount() const
{
    return static_cast<int>(m_Entries.size());
}

IPackage* ProviderRegistryBase::GetAt(int idx) const
{
    if (idx < 0 || idx >= static_cast<int>(m_Entries.size()))
    {
        return nullptr;
    }
    return m_Entries[idx].provider;
}

void ProviderRegistryBase::ClearPluginProviders()
{
    auto it = m_Entries.begin();
    while (it != m_Entries.end())
    {
        if (it->isPlugin)
        {
            if (it->provider)
            {
                delete it->provider;
            }
            it = m_Entries.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void ProviderRegistryBase::ClearAllPluginProviders()
{
    for (auto& kv : RegistryMap())
    {
        if (kv.second)
        {
            kv.second->ClearPluginProviders();
        }
    }
}

}  // namespace Deki
