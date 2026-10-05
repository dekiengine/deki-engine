#pragma once

#include <deki/Component.h>
#include <string>
#include <vector>
#include <cstdint>

#ifdef DEKI_EDITOR
#include <nlohmann/json.hpp>
#endif

// Stands in for a component whose type is not registered (a deleted package,
// a missing plugin). It keeps the raw serialized data, so saving the scene
// writes the component back unchanged.

namespace Deki
{

class MissingComponent : public Component
{
public:
    static constexpr ComponentType kStaticType = HashString("__MissingComponent__");

    MissingComponent()
    {
        // Not a reflected class: it carries its own entry so GetType() answers
        // kStaticType and the scene writers can tell it apart.
        m_Info.name = "__MissingComponent__";
        m_Info.shortName = "MissingComponent";
        m_Info.typeId = kStaticType;
        SetTypeInfo(&m_Info);
    }

    uint32_t originalTypeId = 0;
    std::string typeName;  // The serialized type name from the scene file

    // MessagePack data (runtime path)
    std::vector<uint8_t> rawData;  // Raw msgpack bytes of the data map contents
    uint32_t dataMapSize = 0;      // Number of key-value pairs in the data map

#ifdef DEKI_EDITOR
    // JSON data (editor path)
    nlohmann::json jsonProperties;  // Preserved JSON properties object
#endif

private:
    ComponentTypeInfo m_Info{};
};

}  // namespace Deki
