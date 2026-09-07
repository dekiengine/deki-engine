#pragma once

#include <deki/Component.h>
#include <string>
#include <vector>
#include <cstdint>

#ifdef DEKI_EDITOR
#include <nlohmann/json.hpp>
#endif

/**
 * @brief Placeholder for components whose type no longer exists
 *
 * When a scene references a component type that isn't registered
 * (e.g., deleted package, missing plugin), a MissingComponent is created
 * instead. It preserves the raw serialized data so saving the scene
 * doesn't permanently lose the component data.
 */

namespace Deki
{

class MissingComponent : public Component
{
public:
    static constexpr ComponentType StaticType = HashString("__MissingComponent__");

    MissingComponent()
    {
        // Not a reflected class: it carries its own entry so GetType() answers
        // StaticType and the scene writers can tell it apart.
        m_Info.name = "__MissingComponent__";
        m_Info.shortName = "MissingComponent";
        m_Info.typeId = StaticType;
        SetTypeInfo(&m_Info);
    }

    uint32_t m_OriginalTypeId = 0;
    std::string m_TypeName;  // The serialized type name from the scene file

    // MessagePack data (runtime path)
    std::vector<uint8_t> m_RawData;  // Raw msgpack bytes of the data map contents
    uint32_t m_DataMapSize = 0;      // Number of key-value pairs in the data map

#ifdef DEKI_EDITOR
    // JSON data (editor path)
    nlohmann::json m_JsonProperties;  // Preserved JSON properties object
    std::string m_ClassName;          // Class-name hint carried through from "class" JSON field
#endif

private:
    ComponentTypeInfo m_Info{};
};

}  // namespace Deki
