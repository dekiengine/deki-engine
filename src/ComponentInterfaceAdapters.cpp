#include <deki/ComponentInterfaceAdapters.h>
#include <unordered_map>

namespace Deki
{

namespace ComponentInterfaceAdapters
{

// Key: (interfaceId << 32) | componentType
// This allows O(1) lookup for a specific interface on a specific component type
static std::unordered_map<uint64_t, InterfaceAdapter>& GetRegistry()
{
    static std::unordered_map<uint64_t, InterfaceAdapter> s_Reg;
    return s_Reg;
}

static uint64_t MakeKey(uint32_t interfaceId, ComponentType componentType)
{
    return (static_cast<uint64_t>(interfaceId) << 32) | componentType;
}

static uint32_t s_Version = 1;

void Register(uint32_t interfaceId, ComponentType componentType, InterfaceAdapter adapter)
{
    if (adapter)
    {
        GetRegistry()[MakeKey(interfaceId, componentType)] = adapter;
        ++s_Version;
    }
}

uint32_t Version()
{
    return s_Version;
}

InterfaceAdapter Find(uint32_t interfaceId, ComponentType componentType, ComponentType baseType)
{
    auto& reg = GetRegistry();

    // Try exact type match first
    auto it = reg.find(MakeKey(interfaceId, componentType));
    if (it != reg.end())
    {
        return it->second;
    }

    // Try base type match (for inherited components)
    if (baseType != 0)
    {
        it = reg.find(MakeKey(interfaceId, baseType));
        if (it != reg.end())
        {
            return it->second;
        }
    }

    return nullptr;
}

void* Query(uint32_t interfaceId, Component* comp)
{
    if (!comp)
    {
        return nullptr;
    }
    InterfaceAdapter adapter = Find(interfaceId, comp->GetType(), comp->GetBaseType());
    return adapter ? adapter(comp) : nullptr;
}

}  // namespace ComponentInterfaceAdapters

}  // namespace Deki
