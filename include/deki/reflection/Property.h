#pragma once

// clang-format off
//
// clang-format 18 parses the P2996 reflection attributes below
// ([[=DekiRefl::Exported{}]]) as Objective-C message sends and mangles the
// file. Keep it hand-formatted until clang-format supports P2996. The codegen
// reads these attributes with GCC 16; Clang cannot read them.

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <type_traits>  // For std::remove_reference_t, std::declval in DEKI_PROP_ENUM
#if defined(DEKI_REFLECT)
#include <meta>
#endif

// Node-graph node metadata, defined in deki-nodegraph (DekiNode.h). Declared
// in that package's namespace so a node declared inside another package binds
// to it and not to an undefined global type.
namespace DekiNodeGraph { struct DekiNodeMeta; }

namespace Deki
{

class Component;
class Scene;
struct ComponentTypeInfo;  // Component.h

// Declared here because the generated headers that name it are included
// inside a package's namespace. A `namespace Deki::SceneFormat` written there
// would create `Deki2D::Deki::SceneFormat`, and that inner `Deki` would hide
// the engine's from every lookup in the rest of the package.
namespace SceneFormat { class SceneMsgPackParser; }

// ============================================================================
// Export marker for property generator
// ============================================================================
// Put DEKI_EXPORT above each field the editor inspector should show. The
// generator detects the type from the C++ type and writes .gen.h files with
// PropertyInfo arrays.
//
//   DEKI_EXPORT
//   int32_t width = 0;
//
//   DEKI_EXPORT
//   Deki::Color tint_color;
//
// Show a field only when another field is >= a value:
//   DEKI_EXPORT
//   DEKI_VISIBLE_WHEN(stop_count, 2)
//   float stop2_position;
//
// Clamp to a range, with an input field:
//   DEKI_EXPORT
//   DEKI_RANGE(0, 100)
//   int32_t percentage;
//
// Clamp to a range, with a slider:
//   DEKI_EXPORT
//   DEKI_SLIDER(0.0f, 1.0f)
//   float opacity;
//
// C++26 static reflection.
//
// Only the reflection codegen tool (built with GCC 16, -freflection) defines DEKI_REFLECT.
// There the markers expand to P2996 annotations the generator reads back. In
// every other build (MSVC editor, ESP32) they are empty macros.
//
// String payloads go through std::define_static_string: a raw string-literal
// pointer cannot be read back as a constant, but a define_static_string result
// has static storage and can.
#if defined(DEKI_REFLECT)
namespace DekiRefl {
    struct Exported {};
    struct Unit { const char* name; };                              // DEKI_UNIT(Distance) -> name="Distance"
    struct VisibleWhen { const char* prop; const char* values; };  // controlling prop + stringized value list
    struct EditorOnly {};                                          // DEKI_EDITOR_ONLY: deserialize case is editor-gated
    struct Serializable {};                                         // DEKI_SERIALIZABLE: struct gets Serialize/Deserialize
    struct NoSerialize {};                                         // DEKI_NO_SERIALIZE: field excluded from serialization
    struct Tooltip { const char* text; };                           // DEKI_TOOLTIP("...")
    struct Group { const char* name; };                             // DEKI_GROUP("...")
    struct ToggleObject { const char* field; };                    // DEKI_TOGGLE_OBJECT(field)
    struct Range { const char* lo; const char* hi; bool slider; };  // DEKI_RANGE/DEKI_SLIDER (stringized args)
    struct VisibleWhenRange { const char* prop; const char* lo; const char* hi; };
    struct ObjectName { const char* component; };                  // DEKI_OBJECT_NAME(Comp) -> component="Comp" ("" = any)
    struct ValueOf { const char* prop; };                          // DEKI_VALUE_OF(target) -> prop="target"
    struct RenamedFrom { const char* name; };                      // DEKI_RENAMED_FROM("oldName")
}
#define DEKI_EXPORT                                   [[=::Deki::DekiRefl::Exported{}]]
#define DEKI_EDITOR_ONLY                              [[=::Deki::DekiRefl::EditorOnly{}]]
#define DEKI_UNIT(u)                                  [[=::Deki::DekiRefl::Unit{ std::define_static_string(#u) }]]
#define DEKI_VISIBLE_WHEN(prop, ...)                  [[=::Deki::DekiRefl::VisibleWhen{ std::define_static_string(#prop), std::define_static_string(#__VA_ARGS__) }]]
#define DEKI_TOOLTIP(text)                            [[=::Deki::DekiRefl::Tooltip{ std::define_static_string(text) }]]
#define DEKI_TOGGLE_OBJECT(field)                     [[=::Deki::DekiRefl::ToggleObject{ std::define_static_string(#field) }]]
#define DEKI_VISIBLE_WHEN_RANGE(prop, minVal, maxVal) [[=::Deki::DekiRefl::VisibleWhenRange{ std::define_static_string(#prop), std::define_static_string(#minVal), std::define_static_string(#maxVal) }]]
#define DEKI_RANGE(minVal, maxVal)                    [[=::Deki::DekiRefl::Range{ std::define_static_string(#minVal), std::define_static_string(#maxVal), false }]]
#define DEKI_SLIDER(minVal, maxVal)                   [[=::Deki::DekiRefl::Range{ std::define_static_string(#minVal), std::define_static_string(#maxVal), true }]]
#define DEKI_GROUP(name)                              [[=::Deki::DekiRefl::Group{ std::define_static_string(name) }]]
#define DEKI_OBJECT_NAME(...)                         [[=::Deki::DekiRefl::ObjectName{ std::define_static_string(#__VA_ARGS__) }]]
#define DEKI_VALUE_OF(prop)                           [[=::Deki::DekiRefl::ValueOf{ std::define_static_string(#prop) }]]
#define DEKI_RENAMED_FROM(oldName)                    [[=::Deki::DekiRefl::RenamedFrom{ std::define_static_string(oldName) }]]
#else
#define DEKI_EXPORT
#define DEKI_EDITOR_ONLY
#define DEKI_TOOLTIP(text)
#define DEKI_TOGGLE_OBJECT(field)
#define DEKI_VISIBLE_WHEN(prop, ...)
#define DEKI_VISIBLE_WHEN_RANGE(prop, minVal, maxVal)
#define DEKI_RANGE(minVal, maxVal)
#define DEKI_SLIDER(minVal, maxVal)
#define DEKI_GROUP(name)
// Marks a std::string property as the name of an object in the scene. The node
// graph's property panel draws a picker of the open scene's objects beside the
// text field, which stays typable for names outside it. The optional argument
// limits the picker to objects with that component type or a subclass. Stored
// as componentRefType in the property metadata.
//
//   DEKI_EXPORT DEKI_OBJECT_NAME()                std::string targetObject;
//   DEKI_EXPORT DEKI_OBJECT_NAME(ButtonComponent) std::string buttonObject;
//
// The value is a plain name, so it resolves in any scene that names its
// objects the same way.
#define DEKI_OBJECT_NAME(...)
// Marks a std::string property as the value written to, or compared with, the
// field a PropertyRef property of the same struct picks. The editor draws it
// as that field's type (a drag field for a float, a checkbox for a bool, a
// dropdown for an enum) and writes back the canonical text form. Enums are
// stored as their index, so the device needs no name table.
//
//   DEKI_EXPORT PropertyRef target;
//   DEKI_EXPORT DEKI_VALUE_OF(target) std::string value;
#define DEKI_VALUE_OF(prop)
// Marks a float/Vector2 property as a physical quantity, stored in meters. The
// inspector shows pixels when the project's inspectorUnit is Pixels. Valid
// arguments: Distance, Velocity, Acceleration.
#define DEKI_UNIT(unit)
// A reflected member's name is its key in scene files (see COMPATIBILITY.md).
// To rename one and keep the old key loading:
//
//   DEKI_EXPORT DEKI_RENAMED_FROM("tint_color")
//   Color tintColor;
//
// The codegen adds the old key as an extra deserializer case (device and
// editor) and an extra field-table row. The editor's JSON loader reads the old
// key when the new one is absent and reports it as a legacy key
// (`--verify-scenes` warns), and a save writes the new name. One alias per
// member, and it must differ from the current name.
#define DEKI_RENAMED_FROM(oldName)
#endif

// ============================================================================
// Serializable struct markers (Unity-style automatic serialization)
// ============================================================================
// Put DEKI_SERIALIZABLE before a struct to generate Serialize/Deserialize.
// Every field is serialized except those marked DEKI_NO_SERIALIZE.
//
//   DEKI_SERIALIZABLE
//   struct MyData {
//       int32_t x;
//       std::string name;
//       DEKI_NO_SERIALIZE int32_t cached_value;  // Not serialized
//   };
//
#if defined(DEKI_REFLECT)
#define DEKI_SERIALIZABLE   [[=::Deki::DekiRefl::Serializable{}]]
#define DEKI_NO_SERIALIZE   [[=::Deki::DekiRefl::NoSerialize{}]]
#else
#define DEKI_SERIALIZABLE
#define DEKI_NO_SERIALIZE
#endif
#define DEKI_SERIALIZABLE_NAME(name)
#define DEKI_SERIALIZABLE_DEFAULT_VALUE(val)

/// Physical unit of a Float or Vector2 property.
///
/// Values are stored in the unit noted on each enumerator (meters for
/// lengths). The inspector converts for display by the project's
/// inspectorUnit setting:
///   - Meters: shows the stored value (suffix m / m/s / m/s²).
///   - Pixels: shows stored * pixelsPerMeter (suffix px / px/s / px/s²).
enum class PhysicalUnit : uint8_t {
    None = 0,          // Not a physical quantity (default)
    Distance,          // meters
    Velocity,          // meters per second
    Acceleration,      // meters per second squared
    Angle,             // radians (inspector displays degrees)
    AngularVelocity,   // radians per second (inspector displays degrees/s)
    Temperature,       // degrees Celsius (inspector may display Fahrenheit)
    // Append only, so existing values keep their numbers. Use these instead of
    // a unit in the field name (spiMhz, durationMs), so every package agrees.
    Frequency,         // hertz
    Time               // seconds
};

/// Property types for runtime reflection.
enum class PropertyType : uint8_t {
    // Integer types (signed)
    Int8,
    Int16,
    Int32,
    Int64,
    // Integer types (unsigned)
    UInt8,
    UInt16,
    UInt32,
    UInt64,
    // Floating point
    Float,
    Double,
    // Other primitives
    Bool,
    String,
    // Complex types
    Color,
    Vector2,
    Vector3,
    Enum,
    ObjectRef,  // Another Object, by id (uint32_t)
    Array,      // std::vector<T>; elementType is T's PropertyType
    AssetRef,   // AssetRef<T>: stores the GUID and loads the asset
    NodeRef,    // Another node-graph node, by its graph id (uint32_t)
    PropertyRef // A picked field of a component on an object (see PropertyRef.h)
};

/// Compact field descriptor, the one piece of reflection on every platform.
///
/// PropertyInfo (below) is the editor's full metadata and is compiled only
/// under DEKI_EDITOR; it costs too much flash for a device. Devices still need
/// to know where field X of component Y lives and its type, for data-driven
/// systems (a state machine writing a property, a tween driving a field). This
/// struct says that in 8 bytes per field, in flash.
///
/// Codegen emits one flat table per concrete component (own and inherited
/// fields) and registers it with ComponentRegistry::RegisterFields on all
/// platforms. Look a field up with ComponentRegistry::FindField(typeId, nameHash).
struct FieldRef {
    uint32_t nameHash;   // HashString(field name)
    uint16_t offset;     // offsetof(Component, field)
    uint8_t  type;       // PropertyType
    uint8_t  enumSize;   // sizeof(enum) for Enum fields (1/2/4), else 0
};

/// Property metadata, one per component type and field.
///
/// Meant to live in flash as static const. All string pointers point to
/// string literals.
struct PropertyInfo {
    const char* name;           // Field name
    const char* displayName;    // Editor display name (nullptr = derived from name)
    PropertyType type;
    size_t offset;              // offsetof(Component, field)

    // Constraints
    const char* tooltip;        // nullptr if none
    float minValue;             // Range min (0 if no range)
    float maxValue;             // Range max (0 if no range)
    bool hasRange;              // True if minValue/maxValue are valid

    // For Asset properties: a typeName the owning package registered with
    // AssetTypeRegistry.
    const char* assetType;

    // Enums. `enumValues` names each enumerator and `enumInts` (at the end of
    // the struct) holds each one's underlying value. A position in these
    // tables is not the number stored in the field: an enum may start
    // anywhere, skip values, or be a flags set (1, 2, 4, 8).
    const char* const* enumValues;  // Enumerator names
    int enumCount;
    uint8_t enumSize;               // sizeof(enum): 1, 2 or 4

    // For Array properties
    PropertyType elementType;   // Element type (e.g. Int32, Float, String)

    // Visibility. When visibilityProperty is set, this property is shown only:
    //   - visibilityMask != 0: when bit (1 << value) is set in the mask
    //   - visibilityMask == 0: when value >= visibilityMinValue
    const char* visibilityProperty; // Controlling property (nullptr = always visible)
    int visibilityMinValue;         // Threshold when visibilityMask == 0
    uint32_t visibilityMask;        // Allowed values as bits (0 = use threshold)

    // Object picker. On an ObjectRef property: the component type the picker
    // filters by (from ObjectRef<T>). On a String property: set by
    // DEKI_OBJECT_NAME to mark the field as an object name; non-null enables
    // the picker, "" means no component filter.
    const char* componentRefType;

    // Inspector options
    bool isMultiline;               // Multiline editor for String
    bool triggersAssetRegen;        // Changing it regenerates the related assets
    const char* assetRegenType;     // What to regenerate: "font", "texture", etc.
    bool useSlider;                 // Slider instead of input field (numeric with range)

    const char* group;              // Inspector section header (nullptr = no group)

    // Name of an ObjectRef field whose object's active state this bool
    // property controls (nullptr = none).
    const char* togglesObjectOf;

    // The fields below trail the struct so existing .gen.cpp initializers
    // stay valid (append-only rule). The reflection ABI handshake's
    // sizeofPropertyInfo refuses packages built with an older layout.

    // For inspector display conversion. Only meaningful on Float and Vector2.
    PhysicalUnit physicalUnit = PhysicalUnit::None;

    // DEKI_VALUE_OF: the PropertyRef property this String property holds the
    // value for. The editor picks the value control from what that reference
    // points at.
    const char* valueOfProperty = nullptr;

    // DEKI_RENAMED_FROM: the key this property was saved under before a rename.
    // The JSON loader reads it when the current name is absent.
    const char* renamedFrom = nullptr;

    // Underlying value of each enumerator, parallel to `enumValues`, `enumCount`
    // long. JSON stores the name and resolves it through this table, so
    // reordering an enum does not change what saved files mean. Look the
    // integer up here: the index is not the stored value when an enum starts
    // off zero, has a gap, or is a flags set.
    const int32_t* enumInts = nullptr;
};

/// Name of the enumerator whose underlying value is `value`, or nullptr when
/// the enum has no such value or the property has no `enumInts`.
inline const char* EnumNameForValue(const PropertyInfo& prop, int32_t value)
{
    if (!prop.enumValues || !prop.enumInts) return nullptr;
    for (int i = 0; i < prop.enumCount; ++i)
        if (prop.enumInts[i] == value) return prop.enumValues[i];
    return nullptr;
}

/// True when `a` and `b` are the same name once underscores are dropped and
/// letter case is ignored: "Portrait_180" and "Portrait180", "READ_BINARY"
/// and "ReadBinary".
inline bool SameNameIgnoringStyle(const char* a, const char* b)
{
    auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
    while (true)
    {
        while (*a == '_')
        {
            ++a;
        }
        while (*b == '_')
        {
            ++b;
        }
        if (*a == 0 || *b == 0)
        {
            return *a == *b;
        }
        if (lower(*a) != lower(*b))
        {
            return false;
        }
        ++a;
        ++b;
    }
}

/// Underlying value of the enumerator called `name`. False when the enum has no
/// enumerator by that name, leaving `out` untouched.
///
/// When no name matches exactly, a single enumerator that differs only in
/// underscores and letter case is used, so a name saved in another style
/// (Portrait_180 for Portrait180) still loads.
inline bool EnumValueForName(const PropertyInfo& prop, const char* name, int32_t& out)
{
    if (!prop.enumValues || !prop.enumInts || !name)
    {
        return false;
    }
    for (int i = 0; i < prop.enumCount; ++i)
    {
        const char* candidate = prop.enumValues[i];
        if (candidate && std::strcmp(candidate, name) == 0)
        {
            out = prop.enumInts[i];
            return true;
        }
    }
    int found = -1;
    for (int i = 0; i < prop.enumCount; ++i)
    {
        const char* candidate = prop.enumValues[i];
        if (candidate && SameNameIgnoringStyle(candidate, name))
        {
            if (found >= 0)
            {
                return false;  // two enumerators fit; neither is safe to pick
            }
            found = i;
        }
    }
    if (found < 0)
    {
        return false;
    }
    out = prop.enumInts[found];
    return true;
}

/// Position of `value` in the enum tables, for a UI that works in list indices
/// such as a combo box. -1 when the enum declares no such value.
inline int EnumIndexForValue(const PropertyInfo& prop, int32_t value)
{
    if (!prop.enumInts) return -1;
    for (int i = 0; i < prop.enumCount; ++i)
        if (prop.enumInts[i] == value) return i;
    return -1;
}

/// Component metadata for runtime reflection.
struct ComponentMeta {
    const char* name;           // The identifier alone ("Health"), for display
    const char* serializedName; // The fully qualified C++ name ("Game::Health"): the scene file's "type" key
    const char* category;       // e.g. "2D", "Rendering"
    const char* description;

    uint32_t typeId;            // TypeId<T>() == HashString(serializedName)
    uint32_t baseTypeId;        // Base class ComponentType

    bool isAbstract;            // Cannot be instantiated
    bool isUnique;              // At most one per object
    bool bootSceneOnly;        // Can only be added to boot scenes
    bool platformSceneOnly;    // Can only be added to platform scenes

    size_t instanceSize;        // sizeof(Component)

    // Factory, editor only (may be nullptr at runtime)
    Component* (*createFunc)();

    const PropertyInfo* properties;
    int propertyCount;

    // Display name derived from the class name (CamelCase -> "Camel Case")
    mutable char displayNameBuffer[64];
    mutable bool displayNameGenerated;

    // The fields below trail the struct so existing .gen.cpp initializers
    // stay valid (append-only rule).

    // DEKI_DISPLAY_NAME("...") above the class. When non-null,
    // GetDisplayName() returns it as is.
    const char* displayNameOverride = nullptr;

    // DEKI_SETTINGS_SECTION: the Project Settings section this type provides,
    // or nullptr for an ordinary scene component.
    const char* settingsSection = nullptr;

    // The runtime entry for this class (factory, deserializer, field table).
    const ComponentTypeInfo* typeInfo = nullptr;

    // DEKI_FORMER_NAME: qualified names this component was saved under before,
    // newest first. Scenes store the qualified class name, so without these a
    // rename or a move into a namespace orphans every saved instance. The
    // loader resolves an unknown type through this table and saves the
    // current name.
    const char* const* formerNames = nullptr;
    int formerNameCount = 0;

    // The override when set, otherwise a name derived from `name`.
    const char* GetDisplayName() const {
        if (displayNameOverride) return displayNameOverride;
        if (!displayNameGenerated) {
            GenerateDisplayName();
        }
        return displayNameBuffer;
    }

private:
    void GenerateDisplayName() const {
        const char* src = name;
        char* dst = displayNameBuffer;
        char* end = displayNameBuffer + sizeof(displayNameBuffer) - 1;

        while (*src && dst < end) {
            // A space before an uppercase letter next to a lowercase one, so
            // acronyms stay together ("RTCClock" -> "RTC Clock").
            if (dst != displayNameBuffer && *src >= 'A' && *src <= 'Z') {
                char prev = *(dst - 1);
                char next = *(src + 1);
                bool prevLower = (prev >= 'a' && prev <= 'z');
                bool nextLower = (next >= 'a' && next <= 'z');
                if (prevLower || nextLower) {
                    if (dst < end) *dst++ = ' ';
                }
            }
            *dst++ = *src++;
        }
        *dst = '\0';
        displayNameGenerated = true;
    }
};

// ============================================================================
// Property access helpers
// ============================================================================

template<typename T>
inline T& GetPropertyRef(Component* comp, const PropertyInfo& prop) {
    return *reinterpret_cast<T*>(reinterpret_cast<char*>(comp) + prop.offset);
}

template<typename T>
inline const T& GetPropertyRef(const Component* comp, const PropertyInfo& prop) {
    return *reinterpret_cast<const T*>(reinterpret_cast<const char*>(comp) + prop.offset);
}

template<typename T>
inline T GetPropertyValue(const Component* comp, const PropertyInfo& prop) {
    return *reinterpret_cast<const T*>(reinterpret_cast<const char*>(comp) + prop.offset);
}

template<typename T>
inline void SetPropertyValue(Component* comp, const PropertyInfo& prop, const T& value) {
    *reinterpret_cast<T*>(reinterpret_cast<char*>(comp) + prop.offset) = value;
}

// Node-graph nodes are not Components, so the node canvas reaches their
// fields through a void* base plus offset.
template<typename T>
inline T& GetNodePropertyRef(void* base, const PropertyInfo& prop) {
    return *reinterpret_cast<T*>(reinterpret_cast<char*>(base) + prop.offset);
}

template<typename T>
inline const T& GetNodePropertyRef(const void* base, const PropertyInfo& prop) {
    return *reinterpret_cast<const T*>(reinterpret_cast<const char*>(base) + prop.offset);
}

// ============================================================================
// Component metadata markers (optional, above the class)
// ============================================================================
// A component is any class deriving publicly from Deki::Component. The
// reflection codegen finds it by that, and its identity is its qualified C++
// name (see TypeId.h). These markers add editor metadata. They go on the lines
// right above the class and attach to the next component in the same file:
//
//   DEKI_CATEGORY("Enemies")
//   DEKI_DESCRIPTION("Hit points for anything that can be damaged.")
//   class Health : public Deki::Component
//   {
//   public:
//       DEKI_EXPORT int hp = 10;
//   };
//
// DEKI_CATEGORY      Add Component picker group; defaults to "Game" for project
//                    scripts and "General" for packages.
// DEKI_DESCRIPTION   One line on what the component does, shown under its name
//                    in the picker. Write it for a reader who has never opened
//                    the class.
// DEKI_DISPLAY_NAME  Inspector name, when the one derived from the class name
//                    ("DS3231RTCComponent" -> "DS3231 RTC") is not right.
// DEKI_SETTINGS_SECTION  Makes the component a Project Settings section of
//                    that name instead of a scene component.
// DEKI_FORMER_NAME   A qualified name this component used to have. Scenes
//                    store the qualified class name, so a rename or a move
//                    into a namespace would orphan every saved instance. Add
//                    one line per previous name, oldest last; old scenes then
//                    load and are saved under the current name:
//
//                        DEKI_FORMER_NAME("SpriteComponent")
//                        class Deki2D::SpriteComponent : public RendererComponent
//
//                    Never delete one: some file may still use it. The name
//                    must be unique across the project; the loader reports two
//                    components claiming the same one.
//
// Only the reflection generator sees them; in every other build they expand
// to nothing. Exported fields must be public, because the generated code
// reaches them from outside the class.
#if defined(DEKI_REFLECT)
namespace DekiRefl {
    struct CategoryTag { const char* text; };
    struct DescriptionTag { const char* text; };
    struct DisplayNameTag { const char* text; };
    struct SettingsSectionTag { const char* text; };
    struct FormerNameTag { const char* text; };  // DEKI_FORMER_NAME("Old::Name")
}
#define DEKI_REFL_CONCAT2(a, b) a##b
#define DEKI_REFL_CONCAT(a, b) DEKI_REFL_CONCAT2(a, b)
#define DEKI_REFL_TAG(Tag, text) \
    inline constexpr ::Deki::DekiRefl::Tag DEKI_REFL_CONCAT(kDekiReflTag, __COUNTER__){ std::define_static_string(text) };
#define DEKI_CATEGORY(text)         DEKI_REFL_TAG(CategoryTag, text)
#define DEKI_DESCRIPTION(text)      DEKI_REFL_TAG(DescriptionTag, text)
#define DEKI_DISPLAY_NAME(text)     DEKI_REFL_TAG(DisplayNameTag, text)
#define DEKI_SETTINGS_SECTION(text) DEKI_REFL_TAG(SettingsSectionTag, text)
#define DEKI_FORMER_NAME(text)      DEKI_REFL_TAG(FormerNameTag, text)
#else
#define DEKI_CATEGORY(text)
#define DEKI_DESCRIPTION(text)
#define DEKI_DISPLAY_NAME(text)
#define DEKI_SETTINGS_SECTION(text)
#define DEKI_FORMER_NAME(text)
#endif

// ============================================================================
// Node declaration macro (node-graph nodes, not components)
// ============================================================================
// Put DEKI_NODE inside a node struct and DEKI_EXPORT above each field, then
// include the generated .gen.h at the end of the header. Node types come from
// package and project DLLs (hot-reloadable), and the node canvas draws them
// generically, with no editor rebuild. Node structs have no base class and no
// virtuals (standard-layout), so offsetof-based reflection is valid.
//
//   struct StateNode {
//       DEKI_NODE(StateNode, "StateNode", "MyDomain/States")
//       DEKI_NODE_INPUTS("in")
//       DEKI_NODE_OUTPUTS("done")
//   public:
//       DEKI_EXPORT std::string activityName;
//   };
//   #include "generated/StateNode.gen.h"
//
// Category is "Domain/MenuGroup". The first segment is the graph domain
// (matched against DekiNodeGraphDomain::domainKey); the rest groups the
// add-node menu.
//
// There is no kStaticType here, which would need the string hash in the node
// header. The codegen computes HashString(StaticNodeName) in the .gen.cpp, so
// node headers do not depend on Component.
#ifdef DEKI_EDITOR
    #define DEKI_NODE_REFLECTION_DECLARATIONS() \
        static const ::Deki::PropertyInfo* GetProperties(); \
        static int GetPropertyCount(); \
        static const ::DekiNodeGraph::DekiNodeMeta& GetNodeMeta();
#else
    #define DEKI_NODE_REFLECTION_DECLARATIONS() /* No reflection on embedded */
#endif

#define DEKI_NODE(TypeName, NodeName, Category) \
    public: \
        static constexpr const char* StaticNodeName = NodeName; \
        static constexpr const char* StaticNodeCategory = Category; \
        DEKI_NODE_REFLECTION_DECLARATIONS()

// Optional pin declarations. Labels are only shown in the editor; at runtime
// the interpreter goes by pin indices in the graph's link table. Pass at
// least one label; a node with no pins in a direction leaves the macro out.
// (No __VA_OPT__: the editor builds as C++17.)
#define DEKI_NODE_INPUTS(...) \
    public: \
        static constexpr const char* StaticInputPins[] = { __VA_ARGS__ }; \
        static constexpr int StaticInputPinCount = \
            (int)(sizeof(StaticInputPins) / sizeof(StaticInputPins[0]));

#define DEKI_NODE_OUTPUTS(...) \
    public: \
        static constexpr const char* StaticOutputPins[] = { __VA_ARGS__ }; \
        static constexpr int StaticOutputPinCount = \
            (int)(sizeof(StaticOutputPins) / sizeof(StaticOutputPins[0]));

// Output pin count set at edit time by a DEKI_EXPORT property of this node:
// an Int32 gives its value, an Array its element count. Use instead of
// DEKI_NODE_OUTPUTS. For an Array of String the strings label the pins (e.g.
// an FSM state's transition event names); otherwise pins show their index.
#define DEKI_NODE_DYNAMIC_OUTPUTS(PropName) \
    public: \
        static constexpr const char* StaticDynamicOutputsProperty = PropName;

// The node owns an ordered list of child nodes, all of the given full
// category (e.g. "Fsm/Actions"). Children are edited as a stack in the node's
// inspector (like PlayMaker action lists), saved inside the parent's graph
// entry ({enabled, type, values} each), and loaded at runtime into
// NodeGraphData::NodeInstance::children. Node types in a child category are
// left out of the canvas add-node menu; they live only in a parent's stack.
#define DEKI_NODE_CHILDREN(Category) \
    public: \
        static constexpr const char* StaticNodeChildCategory = Category;

// The node contains an inner graph instead of a child stack. Double-clicking
// it on the canvas opens that graph, and the breadcrumb leads back out.
// `ContentCategory` is the full category the inner add-node menu offers (e.g.
// "Fsm/Actions" for a state's action flow). `EntryTypeName` is the
// StaticNodeName of the entry node placed in every instance's graph ("" for
// none).
//
// The inner graph is a full graph with its own nodes, links and pins, which
// the tool interprets like the root. Node ids are unique across the whole
// document, so an id names one node at any depth.
//
// Use either this or DEKI_NODE_CHILDREN, not both.
#define DEKI_NODE_SUBGRAPH(ContentCategory, EntryTypeName) \
    public: \
        static constexpr const char* StaticNodeSubgraphCategory = ContentCategory; \
        static constexpr const char* StaticNodeSubgraphEntry = EntryTypeName;

// Uses a DEKI_EXPORT String property of this node as its canvas title, or the
// display name while the property is empty. Lets instances have their own
// names ("Attack", "Patrol") instead of all showing the type name.
#define DEKI_NODE_TITLE_PROPERTY(PropName) \
    public: \
        static constexpr const char* StaticNodeTitleProperty = PropName;

// Makes this node type a fixed part of every graph of its domain (e.g. the
// FSM's Awake/Start/Update entries). The domain's default content includes
// it, the editor adds it when opening a graph that lacks it, and it cannot be
// added from the canvas menu or deleted. Its outputs may stay unwired, like
// a lifecycle hook you do not override.
#define DEKI_NODE_PERMANENT() \
    public: \
        static constexpr bool StaticNodePermanent = true;

// This node's child stack (DEKI_NODE_CHILDREN) holds the graph's variables.
// Each child type's DEKI_NODE_TITLE_PROPERTY is the variable's name, and its
// other exported property is the initial value, whose type is the variable's
// type. So there is one child struct per type:
//
//   struct MyNumberVariable {
//       DEKI_NODE(MyNumberVariable, "MyNumberVar", "MyDomain/Variables")
//       DEKI_NODE_TITLE_PROPERTY("name")
//       DEKI_EXPORT std::string name = "value";
//       DEKI_EXPORT float initial = 0.0f;
//   };
//
// The editor offers those names wherever a PropertyRef can point at a
// variable, and the runner allocates storage per instance. Pair with
// DEKI_NODE_PERMANENT so every graph of the domain has exactly one.
#define DEKI_NODE_VARIABLES() \
    public: \
        static constexpr bool StaticNodeVariables = true;

// For one line on what the node does, declare `static constexpr const char*
// kStaticNodeDescription = "Waits until the clock is inside the window.";`
// beside the display name. The add picker shows it under the name, which
// tells similarly named nodes apart.
//
// For a readable editor label (add-node menu and canvas title), declare
// `static constexpr const char* kStaticNodeDisplayName = "Time Window";` in
// the node struct. The codegen puts it in DekiNodeMeta::displayName; without
// it the editor derives one from the node name. StaticNodeName stays the
// serialization id.

}  // namespace Deki
