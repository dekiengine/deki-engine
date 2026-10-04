#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#ifdef DEKI_EDITOR
#include <vector>
#include <functional>
#endif

// Forward declarations

namespace Deki
{

class Scene;
class Object;
class Component;

namespace SceneFormat
{

// MessagePack types we need
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

/**
 * @brief Embedded-friendly MessagePack parser for scene files
 * No dynamic allocation, fixed stack depth
 */
class SceneMsgPackParser
{
public:
    SceneMsgPackParser(const uint8_t* data, size_t size);

    // Parse the scene data into a Scene object (as active/main scene)
    bool ParseScene(Scene* scene);

    // Parse scene data for instancing - returns root object (doesn't set as active)
    Object* ParseSceneInstance();

    // =========================================================================
    // Public value readers (used by component Deserialize methods)
    // =========================================================================

    // Basic reading
    bool ReadByte(uint8_t& value);
    bool PeekByte(uint8_t& value);
    bool ReadBytes(uint8_t* buffer, size_t count);

    // MessagePack value readers
    bool ReadInt(int32_t& value);
    bool ReadFloat(float& value);
    bool ReadBool(bool& value);
    bool ReadString(const char*& str, uint16_t& length);
    bool SkipValue();

    // Map/Array readers
    bool ReadMapSize(uint32_t& size);
    bool ReadArraySize(uint32_t& size);

    // Big-endian primitives. These exist so the shift is written once: the
    // open-coded form was `(b3 << 24) | ...` on uint8_t operands, which promote
    // to int, so any value with the high bit set was signed left-shift overflow
    // (UB) - reachable from any msgpack uint32/int32 >= 0x80000000.
    bool ReadBE16(uint16_t& value);
    bool ReadBE32(uint32_t& value);

    // Shared by ReadMapSize/ReadArraySize, which differ only in their tags.
    bool ReadContainerSize(uint8_t fixPrefix, uint8_t tag16, uint8_t tag32, uint32_t& size);

    // String comparison helper (for null-terminated vs length-prefixed)
    bool StringEquals(const char* msgpackStr, uint16_t length, const char* cstr);

    // Maximum container nesting the parser will follow.
    //
    // A MessagePack fixarray/fixmap header is a single byte, so a run of them
    // nests one level per input byte. Without a cap, SkipValue and
    // ParseObjectInternal recursed once per level and ~20 KB of input exhausted
    // a 1 MB desktop stack - on an ESP32 task stack (commonly 8 KB) a couple of
    // hundred bytes was enough, which a corrupt SD card can produce on its own.
    // Real scenes nest far below this: object trees are a handful deep and
    // component data is one or two levels.
    static constexpr int kMaxDepth = 64;

private:
    const uint8_t* m_Data;
    size_t m_Size;
    size_t m_Pos;
    int m_Depth = 0;  // current container nesting level

    // RAII depth counter. Returns false from Enter() when the limit is hit so
    // the caller can fail the parse instead of recursing further.
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

    // Scene-specific parsers
    bool ParseSceneMetadata(Scene* scene);
    bool ParseObjects(Scene* scene);
    bool ParseObjectInternal(Object*& obj);  // Internal object parsing
    bool ParseTransform(Object* obj);
    bool ParseComponents(Object* obj);
};

// Component creation is now handled by ComponentFactory in reflection/ComponentFactory.h
// Use: ComponentFactory::Instance().Create(typeId)

/**
 * @brief Scene file extension
 */
constexpr const char* kSceneExtension = ".scene";

/**
 * @brief Scene format version
 */
constexpr uint16_t kSceneFormatVersion = 1;

// Field names used in serialization (shared with editor)
namespace Fields
{
constexpr const char* kVersion = "version";
constexpr const char* kName = "name";
constexpr const char* kWidth = "width";
constexpr const char* kHeight = "height";
constexpr const char* kClearColor = "clear_color";
constexpr const char* kObjects = "objects";

// Object fields
constexpr const char* kObjID = "id";
constexpr const char* kObjName = "name";
constexpr const char* kObjActive = "active";
constexpr const char* kObjSortOrder = "sort_order";
constexpr const char* kObjTransform = "transform";
constexpr const char* kObjComponents = "components";
constexpr const char* kObjChildren = "children";
constexpr const char* kObjInstance = "instance";  // New: path to instanced scene

// Transform fields
constexpr const char* kTrX = "x";
constexpr const char* kTrY = "y";
// The third axis, present only when a 3D build set it. A 2D build skips them.
constexpr const char* kTrZ = "z";
constexpr const char* kTrRotationX = "rotationX";
constexpr const char* kTrRotationY = "rotationY";
constexpr const char* kTrScaleZ = "scaleZ";
constexpr const char* kTrRotation = "rotation";  // the Z angle, always
// These were "m_ScaleX" and "scale_y": a rename of Object::scale_x to
// m_ScaleX rewrote the string literal too, putting a C++ private-member prefix
// into the compiled scene format and leaving the four transform keys using
// three different conventions. The authored (JSON) form in Scene.cpp always
// wrote scaleX/scaleY, so this is also what makes the two agree.
//
// Safe to change without a migration only because MessagePack scenes are build
// output - the editor compiles them from the authored .scene JSON on export.
// Authored data is never in this format.
constexpr const char* kTrScaleX = "scaleX";
constexpr const char* kTrScaleY = "scaleY";

// Component fields
constexpr const char* kCompType = "type";
constexpr const char* kCompData = "data";
}  // namespace Fields

#ifdef DEKI_EDITOR
// Function type to resolve asset GUID to runtime path
// Returns empty string if GUID cannot be resolved
using AssetResolver = std::function<std::string(const std::string& guid)>;

// Callback to check if GUID is a subasset and get parent GUID
// Returns true if guid is a subasset, sets outParentGuid to parent
using SubAssetResolverCallback = std::function<bool(const std::string& guid, std::string& outParentGuid)>;

/**
 * @brief MessagePack writer for saving scenes from editor
 */
class SceneMsgPackWriter
{
public:
    SceneMsgPackWriter();

    // Set asset resolver for converting GUIDs to runtime paths
    // When set, Asset properties are written as resolved paths (for runtime export)
    // When not set, Asset properties are written as GUIDs (for editor save)
    void SetAssetResolver(AssetResolver resolver) { m_AssetResolver = resolver; }

    // Set subasset resolver for migrating old scenes with frame GUIDs in guid field
    // When set, checks if AssetRef.guid is a subasset and swaps with parent GUID
    void SetSubAssetResolver(SubAssetResolverCallback resolver) { m_SubAssetResolver = std::move(resolver); }

    // Write scene to MessagePack buffer
    bool WriteScene(Scene* scene);

    // Get the written data
    const uint8_t* GetData() const { return m_Buffer.data(); }
    size_t GetSize() const { return m_Buffer.size(); }

private:
    std::vector<uint8_t> m_Buffer;
    AssetResolver m_AssetResolver;
    SubAssetResolverCallback m_SubAssetResolver;

    // Basic writers
    void WriteByte(uint8_t value);
    void WriteBytes(const uint8_t* data, size_t count);

    // MessagePack value writers
    void WriteNil();
    void WriteBool(bool value);
    void WriteInt(int32_t value);
    void WriteFloat(float value);
    void WriteString(const char* str);
    void WriteString(const std::string& str);

    // Map/Array writers
    void WriteMapSize(uint32_t size);
    void WriteArraySize(uint32_t size);

    // Scene-specific writers
    void WriteSceneMetadata(Scene* scene);
    void WriteObject(Object* obj);
    void WriteTransform(Object* obj);
    void WriteComponents(Object* obj);
    void WriteComponent(Component* comp);
};
#endif  // DEKI_EDITOR

}  // namespace SceneFormat

}  // namespace Deki
