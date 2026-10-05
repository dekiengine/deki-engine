#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#ifdef DEKI_EDITOR
#include <vector>
#endif

namespace Deki
{

class Scene;
class Object;
class Component;

namespace SceneFormat
{

// The MessagePack type tags scene files use.
enum class MPType : uint8_t
{
    Nil = 0xc0,
    False = 0xc2,
    True = 0xc3,
    Float32 = 0xca,
    Int8 = 0xd0,
    Int16 = 0xd1,
    Int32 = 0xd2,
    UInt8 = 0xcc,
    UInt16 = 0xcd,
    UInt32 = 0xce,
    FixStrBase = 0xa0,  // 0xa0-0xbf for strings up to 31 bytes
    Str8 = 0xd9,
    Str16 = 0xda,
    FixMapBase = 0x80,  // 0x80-0x8f for maps up to 15 elements
    Map16 = 0xde,
    FixArrayBase = 0x90,  // 0x90-0x9f for arrays up to 15 elements
    Array16 = 0xdc,
};

/// MessagePack parser for compiled scene files, made for embedded targets: it
/// allocates nothing and its recursion depth is capped (kMaxDepth).
class SceneMsgPackParser
{
public:
    SceneMsgPackParser(const uint8_t* data, size_t size);

    // Fills `scene` from the data.
    bool ParseScene(Scene* scene);

    // Builds the objects for an instance and returns the root, without
    // touching the active scene.
    Object* ParseSceneInstance();

    // =========================================================================
    // Value readers, used by the components' generated Deserialize code
    // =========================================================================

    bool ReadByte(uint8_t& value);
    bool PeekByte(uint8_t& value);
    bool ReadBytes(uint8_t* buffer, size_t count);

    bool ReadInt(int32_t& value);
    bool ReadFloat(float& value);
    bool ReadBool(bool& value);
    bool ReadString(const char*& str, uint16_t& length);
    bool SkipValue();

    bool ReadMapSize(uint32_t& size);
    bool ReadArraySize(uint32_t& size);

    // Big-endian reads. Use these rather than open-coding `(b3 << 24) | ...`:
    // uint8_t operands promote to int, so a value with the high bit set is a
    // signed shift overflow (undefined behaviour).
    bool ReadBE16(uint16_t& value);
    bool ReadBE32(uint32_t& value);

    // Shared by ReadMapSize/ReadArraySize, which differ only in their tags.
    bool ReadContainerSize(uint8_t fixPrefix, uint8_t tag16, uint8_t tag32, uint32_t& size);

    // Compares a length-prefixed MessagePack string with a C string.
    bool StringEquals(const char* msgpackStr, uint16_t length, const char* cstr);

    // Deepest container nesting the parser follows. A fixarray/fixmap header
    // is one byte, so corrupt input (a bad SD card is enough) can nest one
    // level per byte, and SkipValue and ParseObjectInternal recurse per level:
    // a few hundred bytes would overflow an 8 KB ESP32 task stack. Real scenes
    // nest far less: object trees are a handful deep and component data one
    // or two levels.
    static constexpr int kMaxDepth = 64;

private:
    const uint8_t* m_Data;
    size_t m_Size;
    size_t m_Pos;
    int m_Depth = 0;  // current container nesting level

    // Counts nesting for one scope. Enter() returns false at the limit so the
    // caller fails the parse instead of recursing further.
    class DepthGuard
    {
    public:
        explicit DepthGuard(SceneMsgPackParser& p)
            : m_Parser(p),
              m_Entered(false)
        {
        }
        ~DepthGuard()
        {
            if (m_Entered)
            {
                --m_Parser.m_Depth;
            }
        }
        bool Enter()
        {
            if (m_Parser.m_Depth >= kMaxDepth)
            {
                return false;
            }
            ++m_Parser.m_Depth;
            m_Entered = true;
            return true;
        }

    private:
        SceneMsgPackParser& m_Parser;
        bool m_Entered;
    };

    bool ParseSceneMetadata(Scene* scene);
    bool ParseObjects(Scene* scene);
    bool ParseObjectInternal(Object*& obj);
    bool ParseTransform(Object* obj);
    bool ParseComponents(Object* obj);
};

// Components are created by ComponentFactory (reflection/ComponentFactory.h):
// ComponentFactory::Instance().Create(typeId).

constexpr const char* kSceneExtension = ".scene";

constexpr uint16_t kSceneFormatVersion = 1;

// Key names in the compiled format, shared with the editor's writer.
namespace Fields
{
constexpr const char* kVersion = "version";
constexpr const char* kObjects = "objects";

constexpr const char* kObjID = "id";
constexpr const char* kObjName = "name";
constexpr const char* kObjActive = "active";
constexpr const char* kObjTransform = "transform";
constexpr const char* kObjComponents = "components";
constexpr const char* kObjChildren = "children";
constexpr const char* kObjInstance = "instance";  // path to the instanced scene

constexpr const char* kTrX = "x";
constexpr const char* kTrY = "y";
// The third axis, present only when a 3D build wrote it. A 2D build skips them.
constexpr const char* kTrZ = "z";
constexpr const char* kTrRotationX = "rotationX";
constexpr const char* kTrRotationY = "rotationY";
constexpr const char* kTrScaleZ = "scaleZ";
constexpr const char* kTrRotation = "rotation";  // the Z angle, always
// Same keys as the authored JSON form in Scene.cpp. Keys in this format can
// change without a migration because MessagePack scenes are build output,
// compiled from the authored .scene JSON on export; they are not C++ member
// names, so renaming a member must not change them.
constexpr const char* kTrScaleX = "scaleX";
constexpr const char* kTrScaleY = "scaleY";

constexpr const char* kCompType = "type";
constexpr const char* kCompData = "data";
}  // namespace Fields

#ifdef DEKI_EDITOR
/// Writes a scene in the compiled MessagePack format (editor only).
class SceneMsgPackWriter
{
public:
    SceneMsgPackWriter();

    bool WriteScene(Scene* scene);

    const uint8_t* GetData() const { return m_Buffer.data(); }
    size_t GetSize() const { return m_Buffer.size(); }

private:
    std::vector<uint8_t> m_Buffer;

    void WriteByte(uint8_t value);
    void WriteBytes(const uint8_t* data, size_t count);

    void WriteNil();
    void WriteBool(bool value);
    void WriteInt(int32_t value);
    void WriteFloat(float value);
    void WriteString(const char* str);
    void WriteString(const std::string& str);

    void WriteMapSize(uint32_t size);
    void WriteArraySize(uint32_t size);

    void WriteSceneMetadata(Scene* scene);
    void WriteObject(Object* obj);
    void WriteTransform(Object* obj);
    void WriteComponents(Object* obj);
    void WriteComponent(Component* comp);
};
#endif  // DEKI_EDITOR

}  // namespace SceneFormat

}  // namespace Deki
