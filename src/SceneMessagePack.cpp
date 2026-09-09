#include <deki/SceneMessagePack.h>
#include <deki/Scene.h>
#include <deki/Object.h>
#include <deki/Component.h>
#include "MissingComponent.h"
#include "ObjectInternal.h"
#include <deki/reflection/ComponentFactory.h>
#include <cstring>
#include <vector>

#include <deki/LogSystem.h>

#ifdef DEKI_EDITOR
#include <deki/reflection/ComponentRegistry.h>
#include <deki/reflection/Property.h>
#include <deki/assets/AssetManager.h>
#include <deki/assets/AssetRef.h>
#include <deki/Color.h>
#include <deki/Vector.h>
#include <deki/reflection/ObjectRef.h>
#endif

namespace Deki
{


namespace SceneFormat
{

// Parser implementation - needed for both runtime and editor play mode

SceneMsgPackParser::SceneMsgPackParser(const uint8_t* data, size_t size)
    : m_Data(data), m_Size(size), m_Pos(0)
{
}

bool SceneMsgPackParser::ReadByte(uint8_t& value)
{
    if (m_Pos >= m_Size) return false;
    value = m_Data[m_Pos++];
    return true;
}

bool SceneMsgPackParser::PeekByte(uint8_t& value)
{
    if (m_Pos >= m_Size) return false;
    value = m_Data[m_Pos];
    return true;
}

bool SceneMsgPackParser::ReadBytes(uint8_t* buffer, size_t count)
{
    if (m_Pos + count > m_Size) return false;
    memcpy(buffer, m_Data + m_Pos, count);
    m_Pos += count;
    return true;
}

bool SceneMsgPackParser::ReadBE16(uint16_t& value)
{
    uint8_t hi, lo;
    if (!ReadByte(hi) || !ReadByte(lo)) return false;
    value = static_cast<uint16_t>((static_cast<uint32_t>(hi) << 8) | lo);
    return true;
}

bool SceneMsgPackParser::ReadBE32(uint32_t& value)
{
    uint8_t b3, b2, b1, b0;
    if (!ReadByte(b3) || !ReadByte(b2) || !ReadByte(b1) || !ReadByte(b0)) return false;
    // Cast before shifting: uint8_t promotes to int, so b3 << 24 on a byte with
    // the high bit set is signed overflow.
    value = (static_cast<uint32_t>(b3) << 24) | (static_cast<uint32_t>(b2) << 16) |
            (static_cast<uint32_t>(b1) << 8) | static_cast<uint32_t>(b0);
    return true;
}

bool SceneMsgPackParser::ReadContainerSize(uint8_t fixPrefix, uint8_t tag16, uint8_t tag32,
                                           uint32_t& size)
{
    uint8_t b;
    if (!ReadByte(b)) return false;

    // Fixed-size form: the low nibble is the count.
    if ((b & 0xf0) == fixPrefix)
    {
        size = b & 0x0f;
        return true;
    }
    if (b == tag16)
    {
        uint16_t v;
        if (!ReadBE16(v)) return false;
        size = v;
        return true;
    }
    if (b == tag32)
        return ReadBE32(size);

    return false;
}

bool SceneMsgPackParser::ReadMapSize(uint32_t& size)
{
    return ReadContainerSize(0x80, 0xde, 0xdf, size);
}

bool SceneMsgPackParser::ReadArraySize(uint32_t& size)
{
    return ReadContainerSize(0x90, 0xdc, 0xdd, size);
}

bool SceneMsgPackParser::ReadInt(int32_t& value)
{
    uint8_t b;
    if (!ReadByte(b)) return false;

    // Positive fixint (0x00-0x7f)
    if ((b & 0x80) == 0)
    {
        value = b;
        return true;
    }
    // Negative fixint (0xe0-0xff)
    if ((b & 0xe0) == 0xe0)
    {
        value = (int8_t)b;
        return true;
    }
    // uint8
    if (b == 0xcc)
    {
        uint8_t v;
        if (!ReadByte(v)) return false;
        value = v;
        return true;
    }
    // uint16
    if (b == 0xcd)
    {
        uint16_t v;
        if (!ReadBE16(v)) return false;
        value = v;
        return true;
    }
    // uint32
    if (b == 0xce)
    {
        uint32_t v;
        if (!ReadBE32(v)) return false;
        value = static_cast<int32_t>(v);
        return true;
    }
    // int8
    if (b == 0xd0)
    {
        uint8_t v;
        if (!ReadByte(v)) return false;
        value = (int8_t)v;
        return true;
    }
    // int16
    if (b == 0xd1)
    {
        uint16_t v;
        if (!ReadBE16(v)) return false;
        value = static_cast<int16_t>(v);
        return true;
    }
    // int32
    if (b == 0xd2)
    {
        uint32_t v;
        if (!ReadBE32(v)) return false;
        value = static_cast<int32_t>(v);
        return true;
    }

    return false;
}

bool SceneMsgPackParser::ReadFloat(float& value)
{
    uint8_t b;
    if (!PeekByte(b)) return false;

    // Try reading as int first and convert
    if (b != 0xca && b != 0xcb)
    {
        int32_t intVal;
        if (ReadInt(intVal))
        {
            value = (float)intVal;
            return true;
        }
        return false;
    }

    if (!ReadByte(b)) return false;

    // Float32
    if (b == 0xca)
    {
        uint32_t bits;
        if (!ReadBE32(bits)) return false;
        memcpy(&value, &bits, sizeof(float));
        return true;
    }
    // Float64 (convert to float32)
    if (b == 0xcb)
    {
        uint8_t bytes[8];
        if (!ReadBytes(bytes, 8)) return false;
        // Big-endian to native
        uint64_t bits = 0;
        for (int i = 0; i < 8; i++)
        {
            bits = (bits << 8) | bytes[i];
        }
        double dval;
        memcpy(&dval, &bits, sizeof(double));
        value = (float)dval;
        return true;
    }

    return false;
}

bool SceneMsgPackParser::ReadBool(bool& value)
{
    uint8_t b;
    if (!ReadByte(b)) return false;

    if (b == 0xc2)
    {
        value = false;
        return true;
    }
    if (b == 0xc3)
    {
        value = true;
        return true;
    }

    return false;
}

bool SceneMsgPackParser::ReadString(const char*& str, uint16_t& length)
{
    uint8_t b;
    if (!ReadByte(b)) return false;

    // Fixstr (0xa0-0xbf)
    if ((b & 0xe0) == 0xa0)
    {
        length = b & 0x1f;
        str = (const char*)(m_Data + m_Pos);
        m_Pos += length;
        return m_Pos <= m_Size;
    }
    // str8
    if (b == 0xd9)
    {
        uint8_t len;
        if (!ReadByte(len)) return false;
        length = len;
        str = (const char*)(m_Data + m_Pos);
        m_Pos += length;
        return m_Pos <= m_Size;
    }
    // str16
    if (b == 0xda)
    {
        uint8_t hi, lo;
        if (!ReadByte(hi) || !ReadByte(lo)) return false;
        length = (hi << 8) | lo;
        str = (const char*)(m_Data + m_Pos);
        m_Pos += length;
        return m_Pos <= m_Size;
    }

    return false;
}

bool SceneMsgPackParser::SkipValue()
{
    // Every nested container costs a stack frame, and a fixarray/fixmap header
    // is one byte — so without this a small crafted or corrupt file walks the
    // stack off its end. Refusing to descend past kMaxDepth turns that into a
    // failed parse.
    DepthGuard guard(*this);
    if (!guard.Enter())
    {
        DEKI_LOG_ERROR("SceneMsgPackParser: nesting deeper than %d levels — refusing to parse",
                       kMaxDepth);
        return false;
    }

    uint8_t b;
    if (!ReadByte(b)) return false;

    // Nil, false, true
    if (b == 0xc0 || b == 0xc2 || b == 0xc3) return true;

    // Positive fixint, negative fixint
    if ((b & 0x80) == 0 || (b & 0xe0) == 0xe0) return true;

    // Fixstr
    if ((b & 0xe0) == 0xa0)
    {
        m_Pos += b & 0x1f;
        return m_Pos <= m_Size;
    }

    // Fixmap
    if ((b & 0xf0) == 0x80)
    {
        uint32_t count = b & 0x0f;
        for (uint32_t i = 0; i < count * 2; i++)
        {
            if (!SkipValue()) return false;
        }
        return true;
    }

    // Fixarray
    if ((b & 0xf0) == 0x90)
    {
        uint32_t count = b & 0x0f;
        for (uint32_t i = 0; i < count; i++)
        {
            if (!SkipValue()) return false;
        }
        return true;
    }

    // Other types
    switch (b)
    {
        case 0xc4:
        {
            uint8_t len;
            if (!ReadByte(len)) return false;
            m_Pos += len;
            break;
        }  // bin8
        case 0xc5:
        {
            uint8_t hi, lo;
            if (!ReadByte(hi) || !ReadByte(lo)) return false;
            m_Pos += (hi << 8) | lo;
            break;
        }  // bin16
        case 0xca: m_Pos += 4; break;  // float32
        case 0xcb: m_Pos += 8; break;  // float64
        case 0xcc: m_Pos += 1; break;  // uint8
        case 0xcd: m_Pos += 2; break;  // uint16
        case 0xce: m_Pos += 4; break;  // uint32
        case 0xcf: m_Pos += 8; break;  // uint64
        case 0xd0: m_Pos += 1; break;  // int8
        case 0xd1: m_Pos += 2; break;  // int16
        case 0xd2: m_Pos += 4; break;  // int32
        case 0xd3: m_Pos += 8; break;  // int64
        case 0xd9:
        {
            uint8_t len;
            if (!ReadByte(len)) return false;
            m_Pos += len;
            break;
        }  // str8
        case 0xda:
        {
            uint8_t hi, lo;
            if (!ReadByte(hi) || !ReadByte(lo)) return false;
            m_Pos += (hi << 8) | lo;
            break;
        }  // str16
        case 0xdc:
        {  // array16
            uint8_t hi, lo;
            if (!ReadByte(hi) || !ReadByte(lo)) return false;
            uint32_t count = (hi << 8) | lo;
            for (uint32_t i = 0; i < count; i++)
            {
                if (!SkipValue()) return false;
            }
            break;
        }
        case 0xde:
        {  // map16
            uint8_t hi, lo;
            if (!ReadByte(hi) || !ReadByte(lo)) return false;
            uint32_t count = (hi << 8) | lo;
            for (uint32_t i = 0; i < count * 2; i++)
            {
                if (!SkipValue()) return false;
            }
            break;
        }
        default:
            DEKI_LOG_ERROR("SceneMsgPackParser: Unknown MessagePack type 0x%02x", b);
            return false;
    }

    return m_Pos <= m_Size;
}

bool SceneMsgPackParser::StringEquals(const char* msgpackStr, uint16_t length, const char* cstr)
{
    size_t cstrLen = strlen(cstr);
    if (length != cstrLen) return false;
    return strncmp(msgpackStr, cstr, length) == 0;
}

bool SceneMsgPackParser::ParseScene(Scene* scene)
{
    if (!scene || !m_Data || m_Size == 0)
    {
        DEKI_LOG_ERROR("SceneMsgPackParser: Invalid scene or data");
        return false;
    }

    uint32_t mapSize;
    if (!ReadMapSize(mapSize))
    {
        DEKI_LOG_ERROR("SceneMsgPackParser: Scene root is not a map");
        return false;
    }

    for (uint32_t i = 0; i < mapSize; i++)
    {
        const char* key;
        uint16_t keyLen;
        if (!ReadString(key, keyLen))
        {
            DEKI_LOG_ERROR("SceneMsgPackParser: Failed to read field key");
            return false;
        }

        if (StringEquals(key, keyLen, Fields::VERSION))
        {
            int32_t version;
            if (!ReadInt(version)) return false;
            if (version > SCENE_FORMAT_VERSION)
            {
                DEKI_LOG_ERROR("SceneMsgPackParser: Scene version %d is newer than supported %d", version, SCENE_FORMAT_VERSION);
                return false;
            }
        }
        else if (StringEquals(key, keyLen, Fields::NAME))
        {
            // Scene name - skip it (stored externally)
            const char* name;
            uint16_t nameLen;
            if (!ReadString(name, nameLen)) return false;
            // Ignore name
        }
        else if (StringEquals(key, keyLen, Fields::CLEAR_COLOR))
        {
            // Clear color is now in CameraComponent - skip it for backwards compatibility
            uint32_t arrSize;
            if (!ReadArraySize(arrSize) || arrSize != 3)
            {
                DEKI_LOG_ERROR("SceneMsgPackParser: clear_color must be array of 3");
                return false;
            }
            int32_t r, g, b;
            if (!ReadInt(r) || !ReadInt(g) || !ReadInt(b)) return false;
            // Ignore clear color (it's now in CameraComponent)
        }
        else if (StringEquals(key, keyLen, Fields::OBJECTS))
        {
            if (!ParseObjects(scene)) return false;
        }
        else
        {
            // Skip unknown fields
            if (!SkipValue()) return false;
        }
    }

    DEKI_LOG_INTERNAL("SceneMsgPackParser: Loaded scene");
    return true;
}

bool SceneMsgPackParser::ParseObjects(Scene* scene)
{
    uint32_t arrSize;
    if (!ReadArraySize(arrSize))
    {
        DEKI_LOG_ERROR("SceneMsgPackParser: objects is not an array");
        return false;
    }

    DEKI_LOG_INTERNAL("SceneMsgPackParser::ParseObjects: arrSize=%u, scene=%p", arrSize, (void*)scene);

    for (uint32_t i = 0; i < arrSize; i++)
    {
        Object* obj = nullptr;
        if (!ParseObjectInternal(obj)) return false;
        if (obj)
        {
            DEKI_LOG_INTERNAL("SceneMsgPackParser::ParseObjects: AddObject[%u]='%s' to scene=%p",
                              i, obj->GetName().c_str(), (void*)scene);
            scene->AddObject(obj);
        }
    }

    DEKI_LOG_INTERNAL("SceneMsgPackParser::ParseObjects: Done, scene=%p has %zu objects",
                      (void*)scene, scene->GetObjects().size());
    return true;
}

// Deferred component creation data - stores type and data map position
struct DeferredComponent
{
    uint32_t typeId;
    size_t dataPos;           // Position in stream where data map entries start
    size_t dataEndPos;        // Position after all data map entries
    uint32_t dataMapSize;     // Number of key-value pairs in data map
    const char* typeNamePtr;  // Points into parse buffer (valid during parse)
    uint16_t typeNameLen;
};

bool SceneMsgPackParser::ParseObjectInternal(Object*& obj)
{
    // Child objects recurse through here, so the same depth cap that protects
    // SkipValue has to cover the object tree. Each level here also allocates a
    // Object, so an unbounded chain burns heap as well as stack.
    DepthGuard guard(*this);
    if (!guard.Enter())
    {
        DEKI_LOG_ERROR("SceneMsgPackParser: object tree deeper than %d levels — refusing to parse",
                       kMaxDepth);
        return false;
    }

    uint32_t mapSize;
    if (!ReadMapSize(mapSize))
    {
        DEKI_LOG_ERROR("SceneMsgPackParser: object is not a map");
        return false;
    }

    std::string name = "DekiObject";
    uint32_t objectId = 0;
    bool active = true;
    int sortOrder = 0;
    float x = 0, y = 0, rotation = 0, scaleX = 1, scaleY = 1;
    bool hasTransform = false;
    std::vector<DeferredComponent> deferredComponents;
    std::vector<Object*> pendingChildren;

    // Children are parsed (and allocated) before the parent exists, and every
    // key in the loop below can fail the parse. Without this, each malformed
    // scene leaked every child parsed up to the failure — and the depth cap
    // above makes that exit path reachable by design. Disarmed once the
    // children are handed to the parent.
    struct PendingChildCleanup
    {
        std::vector<Object*>& children;
        bool armed = true;
        ~PendingChildCleanup()
        {
            if (!armed) return;
            for (Object* c : children)
                delete c;
            children.clear();
        }
    } childCleanup{ pendingChildren };

    for (uint32_t i = 0; i < mapSize; i++)
    {
        const char* key;
        uint16_t keyLen;
        if (!ReadString(key, keyLen)) return false;

        if (StringEquals(key, keyLen, Fields::OBJ_ID))
        {
            int32_t id;
            if (!ReadInt(id)) return false;
            objectId = static_cast<uint32_t>(id);
        }
        else if (StringEquals(key, keyLen, Fields::OBJ_NAME))
        {
            const char* n;
            uint16_t nLen;
            if (!ReadString(n, nLen)) return false;
            name = std::string(n, nLen);
        }
        else if (StringEquals(key, keyLen, Fields::OBJ_ACTIVE))
        {
            if (!ReadBool(active)) return false;
        }
        else if (StringEquals(key, keyLen, Fields::OBJ_SORT_ORDER))
        {
            int32_t order;
            if (!ReadInt(order)) return false;
            sortOrder = order;
        }
        else if (StringEquals(key, keyLen, Fields::OBJ_TRANSFORM))
        {
            // Parse transform map
            uint32_t trMapSize;
            if (!ReadMapSize(trMapSize)) return false;

            hasTransform = true;
            for (uint32_t j = 0; j < trMapSize; j++)
            {
                const char* trKey;
                uint16_t trKeyLen;
                if (!ReadString(trKey, trKeyLen)) return false;

                if (StringEquals(trKey, trKeyLen, Fields::TR_X))
                {
                    if (!ReadFloat(x)) return false;
                }
                else if (StringEquals(trKey, trKeyLen, Fields::TR_Y))
                {
                    if (!ReadFloat(y)) return false;
                }
                else if (StringEquals(trKey, trKeyLen, Fields::TR_Z))
                {
                    // Deprecated. Engine is 2D-only; silently discard any Z
                    // that an old or hand-edited file might still carry.
                    if (!SkipValue()) return false;
                }
                else if (StringEquals(trKey, trKeyLen, Fields::TR_ROTATION))
                {
                    if (!ReadFloat(rotation)) return false;
                }
                else if (StringEquals(trKey, trKeyLen, Fields::TR_SCALE_X))
                {
                    if (!ReadFloat(scaleX)) return false;
                }
                else if (StringEquals(trKey, trKeyLen, Fields::TR_SCALE_Y))
                {
                    if (!ReadFloat(scaleY)) return false;
                }
                else
                {
                    if (!SkipValue()) return false;
                }
            }
        }
        else if (StringEquals(key, keyLen, Fields::OBJ_COMPONENTS))
        {
            // Parse components array - collect type and data position for deferred creation
            uint32_t compArrSize;
            if (!ReadArraySize(compArrSize)) return false;

            for (uint32_t c = 0; c < compArrSize; c++)
            {
                uint32_t compMapSize;
                if (!ReadMapSize(compMapSize)) return false;

                DeferredComponent deferred = {};

                for (uint32_t cm = 0; cm < compMapSize; cm++)
                {
                    const char* compKey;
                    uint16_t compKeyLen;
                    if (!ReadString(compKey, compKeyLen)) return false;

                    if (StringEquals(compKey, compKeyLen, Fields::COMP_TYPE))
                    {
                        const char* typeStr;
                        uint16_t typeLen;
                        if (!ReadString(typeStr, typeLen)) return false;
                        // Hash the type name to get type ID
                        deferred.typeId = HashStringLen(typeStr, typeLen);
                        deferred.typeNamePtr = typeStr;
                        deferred.typeNameLen = typeLen;
                    }
                    else if (StringEquals(compKey, compKeyLen, Fields::COMP_DATA))
                    {
                        // Record position and size of data map for deferred deserialization
                        uint32_t dataMapSize;
                        if (!ReadMapSize(dataMapSize)) return false;
                        deferred.dataPos = m_Pos;
                        deferred.dataMapSize = dataMapSize;
                        // Skip the data for now - we'll deserialize later
                        for (uint32_t d = 0; d < dataMapSize; d++)
                        {
                            if (!SkipValue()) return false;  // key
                            if (!SkipValue()) return false;  // value
                        }
                        deferred.dataEndPos = m_Pos;
                    }
                    else
                    {
                        if (!SkipValue()) return false;
                    }
                }

                if (deferred.typeId != 0)
                {
                    deferredComponents.push_back(deferred);
                }
            }
        }
        else if (StringEquals(key, keyLen, Fields::OBJ_CHILDREN))
        {
            // Parse children recursively
            uint32_t childArrSize;
            if (!ReadArraySize(childArrSize)) return false;
            for (uint32_t ci = 0; ci < childArrSize; ci++)
            {
                Object* child = nullptr;
                if (!ParseObjectInternal(child)) return false;
                if (child)
                {
                    pendingChildren.push_back(child);
                }
            }
        }
        else if (StringEquals(key, keyLen, Fields::OBJ_INSTANCE))
        {
            // Instance field - path to nested scene (future feature)
            if (!SkipValue()) return false;
        }
        else
        {
            if (!SkipValue()) return false;
        }
    }

    // Create the object
    // Note: sortOrder from legacy files is ignored, now stored in RendererComponent
    obj = new Object(name);
    if (objectId != 0)
    {
        obj->SetId(objectId);  // Preserve ID for ObjectRef resolution
    }
    obj->SetActive(active);

#ifdef DEKI_TRANSFORM_2D
    if (hasTransform)
    {
        obj->SetLocalPosition(x, y);
        obj->SetRotation(rotation);
        obj->SetScale(scaleX, scaleY);
    }
#else
    // No transform in this build. The map was consumed so the parse stays in
    // step with the file; the values are simply not kept.
    (void)hasTransform; (void)x; (void)y; (void)rotation; (void)scaleX; (void)scaleY;
#endif

    // Create components using factory and Deserialize
    for (const auto& deferred : deferredComponents)
    {
        Component* comp = ComponentFactory::Instance().Create(deferred.typeId);
        if (comp)
        {
            // Rewind parser to component data position and deserialize
            size_t savedPos = m_Pos;
            m_Pos = deferred.dataPos;

            if (!comp->Deserialize(*this, deferred.dataMapSize))
            {
                DEKI_LOG_WARNING("SceneMsgPackParser: Failed to deserialize component '%.*s' (typeId: 0x%08x, mapSize: %u) on object '%s'",
                                 (int)deferred.typeNameLen, deferred.typeNamePtr, deferred.typeId, deferred.dataMapSize, name.c_str());
            }
            m_Pos = savedPos;  // Restore position (already skipped past data)

            Internal::AttachComponent(*obj, comp);
            DEKI_LOG_INTERNAL("SceneMsgPackParser: Added component (typeId: 0x%08x) to '%s'",
                              deferred.typeId, name.c_str());
        }
        else
        {
            // Unknown type — create MissingComponent to preserve data
            auto* missing = new MissingComponent();
            missing->m_OriginalTypeId = deferred.typeId;
            missing->m_TypeName = std::string(deferred.typeNamePtr, deferred.typeNameLen);
            missing->m_DataMapSize = deferred.dataMapSize;
            if (deferred.dataEndPos > deferred.dataPos)
            {
                size_t rawSize = deferred.dataEndPos - deferred.dataPos;
                missing->m_RawData.assign(m_Data + deferred.dataPos, m_Data + deferred.dataEndPos);
            }
            Internal::AttachComponent(*obj, missing);
            DEKI_LOG_WARNING("SceneMsgPackParser: Missing component type '%.*s' (typeId: 0x%08x) on '%s' — data preserved",
                             (int)deferred.typeNameLen, deferred.typeNamePtr, deferred.typeId, name.c_str());
        }
    }

    // Add children (parsed recursively) — forms parent-child hierarchy.
    // Ownership passes to the parent here, so the cleanup guard stands down.
    for (Object* child : pendingChildren)
    {
        obj->AddChild(child);
    }
    childCleanup.armed = false;

    return true;
}

Object* SceneMsgPackParser::ParseSceneInstance()
{
    // Scene instance format is just a single object (same as scene object format)
    // The data should be a map representing the root object
    Object* obj = nullptr;
    if (!ParseObjectInternal(obj))
    {
        DEKI_LOG_ERROR("SceneMsgPackParser: Failed to parse scene instance");
        return nullptr;
    }
    return obj;
}

#ifdef DEKI_EDITOR

// ============================================================================
// MessagePack Writer (Editor only)
// ============================================================================

SceneMsgPackWriter::SceneMsgPackWriter()
{
    m_Buffer.reserve(4096);  // Pre-allocate reasonable size
}

void SceneMsgPackWriter::WriteByte(uint8_t value)
{
    m_Buffer.push_back(value);
}

void SceneMsgPackWriter::WriteBytes(const uint8_t* data, size_t count)
{
    m_Buffer.insert(m_Buffer.end(), data, data + count);
}

void SceneMsgPackWriter::WriteNil()
{
    WriteByte(0xc0);
}

void SceneMsgPackWriter::WriteBool(bool value)
{
    WriteByte(value ? 0xc3 : 0xc2);
}

void SceneMsgPackWriter::WriteInt(int32_t value)
{
    if (value >= 0 && value <= 127)
    {
        // Positive fixint
        WriteByte((uint8_t)value);
    }
    else if (value >= -32 && value < 0)
    {
        // Negative fixint
        WriteByte((uint8_t)value);
    }
    else if (value >= 0 && value <= 255)
    {
        // uint8
        WriteByte(0xcc);
        WriteByte((uint8_t)value);
    }
    else if (value >= 0 && value <= 65535)
    {
        // uint16
        WriteByte(0xcd);
        WriteByte((uint8_t)(value >> 8));
        WriteByte((uint8_t)value);
    }
    else if (value >= -128 && value <= 127)
    {
        // int8
        WriteByte(0xd0);
        WriteByte((uint8_t)value);
    }
    else if (value >= -32768 && value <= 32767)
    {
        // int16
        WriteByte(0xd1);
        WriteByte((uint8_t)(value >> 8));
        WriteByte((uint8_t)value);
    }
    else
    {
        // int32
        WriteByte(0xd2);
        WriteByte((uint8_t)(value >> 24));
        WriteByte((uint8_t)(value >> 16));
        WriteByte((uint8_t)(value >> 8));
        WriteByte((uint8_t)value);
    }
}

void SceneMsgPackWriter::WriteFloat(float value)
{
    WriteByte(0xca);
    uint32_t bits;
    memcpy(&bits, &value, sizeof(float));
    // Write big-endian
    WriteByte((uint8_t)(bits >> 24));
    WriteByte((uint8_t)(bits >> 16));
    WriteByte((uint8_t)(bits >> 8));
    WriteByte((uint8_t)bits);
}

void SceneMsgPackWriter::WriteString(const char* str)
{
    if (!str)
        str = "";
    size_t len = strlen(str);
    if (len <= 31)
    {
        // Fixstr
        WriteByte(0xa0 | (uint8_t)len);
    }
    else if (len <= 255)
    {
        // str8
        WriteByte(0xd9);
        WriteByte((uint8_t)len);
    }
    else
    {
        // str16
        WriteByte(0xda);
        WriteByte((uint8_t)(len >> 8));
        WriteByte((uint8_t)len);
    }
    WriteBytes((const uint8_t*)str, len);
}

void SceneMsgPackWriter::WriteString(const std::string& str)
{
    WriteString(str.c_str());
}

void SceneMsgPackWriter::WriteMapSize(uint32_t size)
{
    if (size <= 15)
    {
        // Fixmap
        WriteByte(0x80 | (uint8_t)size);
    }
    else
    {
        // Map16
        WriteByte(0xde);
        WriteByte((uint8_t)(size >> 8));
        WriteByte((uint8_t)size);
    }
}

void SceneMsgPackWriter::WriteArraySize(uint32_t size)
{
    if (size <= 15)
    {
        // Fixarray
        WriteByte(0x90 | (uint8_t)size);
    }
    else
    {
        // Array16
        WriteByte(0xdc);
        WriteByte((uint8_t)(size >> 8));
        WriteByte((uint8_t)size);
    }
}

bool SceneMsgPackWriter::WriteScene(Scene* scene)
{
    if (!scene) return false;

    m_Buffer.clear();

    // Scene is a map with: version, objects
    // (name and clearColor are no longer stored - clearColor is now in CameraComponent)
    WriteMapSize(2);

    // Version
    WriteString(Fields::VERSION);
    WriteInt(SCENE_FORMAT_VERSION);

    // Objects (skip editor-only objects)
    WriteString(Fields::OBJECTS);
    const auto& objects = scene->GetObjects();

    // Count non-editor-only objects first (MessagePack needs array size upfront)
    uint32_t objectCount = 0;
    for (Object* obj : objects)
    {
        if (obj && !obj->IsEditorOnly())
        {
            objectCount++;
        }
    }
    WriteArraySize(objectCount);

    for (Object* obj : objects)
    {
        if (obj && !obj->IsEditorOnly())
        {
            WriteObject(obj);
        }
    }

    return true;
}

void SceneMsgPackWriter::WriteObject(Object* obj)
{
    DEKI_LOG_INTERNAL("SceneMsgPackWriter::WriteObject: '%s' components=%zu children=%zu instance=%d",
                      obj->GetName().c_str(), obj->GetComponents().size(), obj->GetChildren().size(),
                      obj->IsSceneInstance());

    // Count non-editor-only children
    const auto& children = obj->GetChildren();
    uint32_t childCount = 0;
    for (Object* child : children)
    {
        if (child && !child->IsEditorOnly()) childCount++;
    }

    // 5 base fields + children (only if non-empty to keep leaf objects compact)
    uint32_t fieldCount = 5;
    if (childCount > 0) fieldCount = 6;
    WriteMapSize(fieldCount);

    // ID (required for ObjectRef resolution at runtime)
    WriteString(Fields::OBJ_ID);
    WriteInt(static_cast<int32_t>(obj->GetId()));

    // Name
    WriteString(Fields::OBJ_NAME);
    WriteString(obj->GetName());

    // Active
    WriteString(Fields::OBJ_ACTIVE);
    WriteBool(obj->IsActive());

    // Transform
    WriteString(Fields::OBJ_TRANSFORM);
    WriteTransform(obj);

    // Components
    WriteString(Fields::OBJ_COMPONENTS);
    WriteComponents(obj);

    // Children (recursive)
    if (childCount > 0)
    {
        WriteString(Fields::OBJ_CHILDREN);
        WriteArraySize(childCount);
        for (Object* child : children)
        {
            if (child && !child->IsEditorOnly())
            {
                WriteObject(child);
            }
        }
    }
}

void SceneMsgPackWriter::WriteTransform(Object* obj)
{
#ifndef DEKI_TRANSFORM_2D
    // Nothing to write. The value stays a map so the reader's structure holds.
    (void)obj;
    WriteMapSize(0);
#else
    WriteMapSize(5);

    WriteString(Fields::TR_X);
    WriteFloat(obj->GetX());

    WriteString(Fields::TR_Y);
    WriteFloat(obj->GetY());

    WriteString(Fields::TR_ROTATION);
    WriteFloat(obj->GetLocalRotation());

    WriteString(Fields::TR_SCALE_X);
    WriteFloat(obj->GetScaleX());

    WriteString(Fields::TR_SCALE_Y);
    WriteFloat(obj->GetScaleY());
#endif
}

void SceneMsgPackWriter::WriteComponents(Object* obj)
{
    const auto& components = obj->GetComponents();
    WriteArraySize((uint32_t)components.size());

    for (Component* comp : components)
    {
        WriteComponent(comp);
    }
}

void SceneMsgPackWriter::WriteComponent(Component* comp)
{
    // Handle MissingComponent — write preserved raw data
    if (comp->GetType() == MissingComponent::StaticType)
    {
        auto* missing = static_cast<MissingComponent*>(comp);
        WriteMapSize(2);
        WriteString(Fields::COMP_TYPE);
        WriteString(missing->m_TypeName);
        WriteString(Fields::COMP_DATA);
        WriteMapSize(missing->m_DataMapSize);
        if (!missing->m_RawData.empty())
        {
            WriteBytes(missing->m_RawData.data(), missing->m_RawData.size());
        }
        return;
    }

    // Each component is a map with: type, data
    WriteMapSize(2);

    WriteString(Fields::COMP_TYPE);

    // Get component type name from registry
    auto& registry = ComponentRegistry::Instance();
    const ComponentMeta* meta = registry.GetMeta(comp->GetType());
    const char* typeName = meta ? meta->serializedName : "Unknown";
    WriteString(typeName);

    WriteString(Fields::COMP_DATA);

    // Collect all properties including the base-class chain (derived class first, then bases)
    std::vector<const PropertyInfo*> allProps;
    {
        const ComponentMeta* current = meta;
        while (current)
        {
            for (int i = 0; i < current->propertyCount; ++i)
                allProps.push_back(&current->properties[i]);
            current = (current->baseTypeId != 0) ? registry.GetMeta(current->baseTypeId) : nullptr;
        }
    }

    // Write component properties using reflection
    if (!allProps.empty())
    {
        WriteMapSize((uint32_t)allProps.size());
        for (const PropertyInfo* propPtr : allProps)
        {
            const PropertyInfo& prop = *propPtr;
            WriteString(prop.name);

            switch (prop.type)
            {
                case PropertyType::Int8:
                    WriteInt(static_cast<int32_t>(GetPropertyValue<int8_t>(comp, prop)));
                    break;
                case PropertyType::Int16:
                    WriteInt(static_cast<int32_t>(GetPropertyValue<int16_t>(comp, prop)));
                    break;
                case PropertyType::Int32:
                    WriteInt(GetPropertyValue<int32_t>(comp, prop));
                    break;
                case PropertyType::Int64:
                    WriteInt(static_cast<int32_t>(GetPropertyValue<int64_t>(comp, prop)));
                    break;
                case PropertyType::UInt8:
                    WriteInt(static_cast<int32_t>(GetPropertyValue<uint8_t>(comp, prop)));
                    break;
                case PropertyType::UInt16:
                    WriteInt(static_cast<int32_t>(GetPropertyValue<uint16_t>(comp, prop)));
                    break;
                case PropertyType::UInt32:
                    WriteInt(static_cast<int32_t>(GetPropertyValue<uint32_t>(comp, prop)));
                    break;
                case PropertyType::UInt64:
                    WriteInt(static_cast<int32_t>(GetPropertyValue<uint64_t>(comp, prop)));
                    break;
                case PropertyType::Float:
                    WriteFloat(GetPropertyValue<float>(comp, prop));
                    break;
                case PropertyType::Double:
                    WriteFloat(static_cast<float>(GetPropertyValue<double>(comp, prop)));
                    break;
                case PropertyType::Bool:
                    WriteBool(GetPropertyValue<bool>(comp, prop));
                    break;
                case PropertyType::String:
                    WriteString(GetPropertyValue<std::string>(comp, prop));
                    break;
                case PropertyType::Color:
                    WriteInt((int32_t)GetPropertyRef<Deki::Color>(comp, prop).ToRGBA8888());
                    break;
                case PropertyType::Enum:
                    switch (prop.enumSize)
                    {
                        case 1: WriteInt(static_cast<int32_t>(GetPropertyValue<uint8_t>(comp, prop))); break;
                        case 2: WriteInt(static_cast<int32_t>(GetPropertyValue<uint16_t>(comp, prop))); break;
                        default: WriteInt(GetPropertyValue<int32_t>(comp, prop)); break;
                    }
                    break;
                case PropertyType::Vector2:
                {
                    const Vector2& v = GetPropertyRef<Vector2>(comp, prop);
                    WriteArraySize(2);
                    WriteFloat(v.x);
                    WriteFloat(v.y);
                    break;
                }
                case PropertyType::Vector3:
                {
                    const Vector3& v = GetPropertyRef<Vector3>(comp, prop);
                    WriteArraySize(3);
                    WriteFloat(v.x);
                    WriteFloat(v.y);
                    WriteFloat(v.z);
                    break;
                }
                case PropertyType::Asset:
                {
                    // Asset properties store GUIDs in editor
                    // When exporting for runtime, resolve GUID to path
                    std::string guid = GetPropertyValue<std::string>(comp, prop);
                    if (m_AssetResolver && !guid.empty())
                    {
                        std::string path = m_AssetResolver(guid);
                        WriteString(path);
                    }
                    else
                    {
                        // No resolver or empty GUID - write as-is
                        WriteString(guid);
                    }
                    break;
                }
                case PropertyType::ObjectRef:
                {
                    const ObjectRefBase& ref = GetPropertyRef<ObjectRefBase>(comp, prop);
                    if (ref.componentTypeId != 0)
                    {
                        WriteArraySize(2);
                        WriteInt(static_cast<int32_t>(ref.id));
                        WriteInt(static_cast<int32_t>(ref.componentTypeId));
                    }
                    else
                    {
                        WriteInt(static_cast<int32_t>(ref.id));
                    }
                    break;
                }
                case PropertyType::Array:
                {
                    // Handle array based on element type
                    if (prop.elementType == PropertyType::ObjectRef)
                    {
                        const auto& refList = GetPropertyRef<std::vector<ObjectRefBase>>(comp, prop);
                        WriteArraySize(static_cast<uint32_t>(refList.size()));
                        for (const auto& ref : refList)
                        {
                            if (ref.componentTypeId != 0)
                            {
                                WriteArraySize(2);
                                WriteInt(static_cast<int32_t>(ref.id));
                                WriteInt(static_cast<int32_t>(ref.componentTypeId));
                            }
                            else
                            {
                                WriteInt(static_cast<int32_t>(ref.id));
                            }
                        }
                    }
                    else
                    {
                        // Default: String list
                        const std::vector<std::string>& stringList = GetPropertyRef<std::vector<std::string>>(comp, prop);
                        WriteArraySize(static_cast<uint32_t>(stringList.size()));
                        for (const auto& str : stringList)
                        {
                            WriteString(str);
                        }
                    }
                    break;
                }
                case PropertyType::AssetRef:
                {
                    // AssetRef stores both guid (for loading) and source (for subasset/frame lookup)
                    // Write as a map with two keys: "guid" and "source"
                    const Deki::AssetRefBase& ref = GetPropertyRef<Deki::AssetRefBase>(comp, prop);

                    std::string guidToWrite = ref.guid;
                    std::string sourceToWrite = ref.source;

                    // Migration: If guid is a subasset (frame GUID), swap it with parent
                    // This fixes old scenes where frame GUIDs were stored in guid field
                    if (m_SubAssetResolver && !ref.guid.empty() && ref.source.empty())
                    {
                        std::string parentGuid;
                        if (m_SubAssetResolver(ref.guid, parentGuid))
                        {
                            // guid was a frame GUID - fix it
                            sourceToWrite = ref.guid;  // Frame GUID goes to source
                            guidToWrite = parentGuid;  // Parent GUID goes to guid
                        }
                    }

                    DEKI_LOG_INTERNAL("SceneMsgPackWriter: AssetRef '%s' -> guid='%s', source='%s'",
                                      prop.name, guidToWrite.c_str(), sourceToWrite.c_str());

                    WriteMapSize(2);
                    WriteString("guid");
                    WriteString(guidToWrite);
                    WriteString("source");
                    WriteString(sourceToWrite);
                    break;
                }
                default:
                    DEKI_LOG_ERROR("SceneMsgPackWriter: Unhandled property type %d for '%s' on '%s'",
                                   (int)prop.type, prop.name, typeName);
                    WriteNil();
                    break;
            }
        }
    }
    else
    {
        // Empty data map for components without properties
        WriteMapSize(0);
    }
}

#endif  // DEKI_EDITOR

}  // namespace SceneFormat

}  // namespace Deki
