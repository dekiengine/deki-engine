#include <deki/Component.h>
#include <deki/SceneMessagePack.h>

namespace Deki
{

bool Component::Deserialize(SceneFormat::SceneMsgPackParser& parser, uint32_t mapSize)
{
    if (m_TypeInfo && m_TypeInfo->deserialize)
    {
        return m_TypeInfo->deserialize(this, parser, mapSize);
    }

    // No deserializer for this class: consume the data map so the parser stays
    // positioned on whatever follows the component.
    for (uint32_t i = 0; i < mapSize; ++i)
    {
        const char* key;
        uint16_t keyLen;
        if (!parser.ReadString(key, keyLen))
        {
            return false;
        }
        if (!parser.SkipValue())
        {
            return false;
        }
    }
    return true;
}

}  // namespace Deki
