#pragma once

// clang-format off
//
// Excluded on purpose. The P2996 reflection attributes below
// ([[=deki_refl::exported{}]]) are parsed by clang-format 18 as Objective-C
// message sends, so it either refuses to run or reformats this file by
// Objective-C rules -- inconsistently with the rest of the codebase, and over
// syntax it does not actually understand. The engine's codegen needs GCC 16
// for these attributes; Clang cannot read them at all.
//
// Keep this file hand-formatted until clang-format supports P2996.

#include <cstdint>
#include <cstddef>
#include <type_traits>  // For std::remove_reference_t, std::declval in DEKI_PROP_ENUM
#if defined(DEKI_REFLECT)
#include <meta>
#endif

// Forward declarations

// Node-graph node metadata. The definition is deki-nodegraph's
// (DekiNode.h), so the declaration names that package's namespace: a node
// declared inside its own package would otherwise bind this to a global type
// that is never defined.
namespace DekiNodeGraph { struct DekiNodeMeta; }

namespace Deki
{

class Component;
class Scene;
struct ComponentTypeInfo;  // Component.h

// Declared here, at global scope, because the generated headers that name it
// are included INSIDE a package's namespace, next to the type they belong to.
// A `namespace Deki::SceneFormat` written from in there would create
// `Deki2D::Deki::SceneFormat` instead, and that inner `Deki` then hides the
// engine's from every lookup in the rest of the package.
namespace SceneFormat { class SceneMsgPackParser; }

// ============================================================================
// Export marker for property generator
// ============================================================================
// Place DEKI_EXPORT above each field to expose in the editor inspector.
// Type is auto-detected from the C++ variable type by the generator.
// The generator produces .gen.h files with PropertyInfo arrays.
//
// Usage:
//   DEKI_EXPORT
//   int32_t width = 0;
//
//   DEKI_EXPORT
//   Deki::Color tint_color;
//
// For conditional visibility (property shown when controlling property >= minValue):
//   DEKI_EXPORT
//   DEKI_VISIBLE_WHEN(stop_count, 2)
//   float stop2_position;
//
// For range constraints (clamps value, shows input field):
//   DEKI_EXPORT
//   DEKI_RANGE(0, 100)
//   int32_t percentage;
//
// For slider UI (shows slider instead of input field):
//   DEKI_EXPORT
//   DEKI_SLIDER(0.0f, 1.0f)
//   float opacity;
//
// C++26 static-reflection seam.
//
// Only the standalone reflection codegen tool (built with GCC 16.1+) defines DEKI_REFLECT.
// For it, the inspector markers expand to P2996 annotations so the generator can read them
// back. MSVC (editor) and the ESP32 toolchain never define DEKI_REFLECT, so for those builds
// every marker stays an empty macro and the headers are byte-for-byte unchanged.
//
// Annotation string payloads are wrapped in std::define_static_string: a raw string-literal
// pointer cannot be reconstituted as a constant when extracted, but a define_static_string
// result has stable static storage and round-trips.
#if defined(DEKI_REFLECT)
namespace deki_refl {
    struct exported {};
    struct unit { const char* name; };                              // DEKI_UNIT(Distance) -> name="Distance"
    struct visible_when { const char* prop; const char* values; };  // controlling prop + stringized value list
    struct editor_only {};                                          // DEKI_EDITOR_ONLY: deserialize case is editor-gated
    struct serializable {};                                         // DEKI_SERIALIZABLE: struct gets Serialize/Deserialize
    struct no_serialize {};                                         // DEKI_NO_SERIALIZE: field excluded from serialization
    struct tooltip { const char* text; };                           // DEKI_TOOLTIP("...")
    struct group { const char* name; };                             // DEKI_GROUP("...")
    struct toggle_object { const char* field; };                    // DEKI_TOGGLE_OBJECT(field)
    struct range { const char* lo; const char* hi; bool slider; };  // DEKI_RANGE/DEKI_SLIDER (stringized args)
    struct visible_when_range { const char* prop; const char* lo; const char* hi; };
    struct object_name { const char* component; };                  // DEKI_OBJECT_NAME(Comp) -> component="Comp" ("" = any)
    struct value_of { const char* prop; };                          // DEKI_VALUE_OF(target) -> prop="target"
    struct renamed_from { const char* name; };                      // DEKI_RENAMED_FROM("oldName")
}
#define DEKI_EXPORT                                   [[=::Deki::deki_refl::exported{}]]
#define DEKI_EDITOR_ONLY                              [[=::Deki::deki_refl::editor_only{}]]
#define DEKI_UNIT(u)                                  [[=::Deki::deki_refl::unit{ std::define_static_string(#u) }]]
#define DEKI_VISIBLE_WHEN(prop, ...)                  [[=::Deki::deki_refl::visible_when{ std::define_static_string(#prop), std::define_static_string(#__VA_ARGS__) }]]
#define DEKI_TOOLTIP(text)                            [[=::Deki::deki_refl::tooltip{ std::define_static_string(text) }]]
#define DEKI_TOGGLE_OBJECT(field)                     [[=::Deki::deki_refl::toggle_object{ std::define_static_string(#field) }]]
#define DEKI_VISIBLE_WHEN_RANGE(prop, minVal, maxVal) [[=::Deki::deki_refl::visible_when_range{ std::define_static_string(#prop), std::define_static_string(#minVal), std::define_static_string(#maxVal) }]]
#define DEKI_RANGE(minVal, maxVal)                    [[=::Deki::deki_refl::range{ std::define_static_string(#minVal), std::define_static_string(#maxVal), false }]]
#define DEKI_SLIDER(minVal, maxVal)                   [[=::Deki::deki_refl::range{ std::define_static_string(#minVal), std::define_static_string(#maxVal), true }]]
#define DEKI_GROUP(name)                              [[=::Deki::deki_refl::group{ std::define_static_string(name) }]]
#define DEKI_OBJECT_NAME(...)                         [[=::Deki::deki_refl::object_name{ std::define_static_string(#__VA_ARGS__) }]]
#define DEKI_VALUE_OF(prop)                           [[=::Deki::deki_refl::value_of{ std::define_static_string(#prop) }]]
#define DEKI_RENAMED_FROM(oldName)                    [[=::Deki::deki_refl::renamed_from{ std::define_static_string(oldName) }]]
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
// Mark a std::string property as the NAME of an object in the scene. The node
// graph's property panel draws a picker beside the text field, listing the open
// scene's hierarchy (the field stays typable for names outside it). The
// optional argument filters the picker to objects carrying that component type
// (or a subclass of it); no argument lists every object. Reads as
// componentRefType in the property metadata.
//
//   DEKI_EXPORT DEKI_OBJECT_NAME()                std::string targetObject;
//   DEKI_EXPORT DEKI_OBJECT_NAME(ButtonComponent) std::string buttonObject;
//
// The stored value stays a plain name, so a graph/component authored against
// one scene still resolves in another that names its objects the same way.
#define DEKI_OBJECT_NAME(...)
// Mark a std::string property as the literal VALUE written to / compared with
// the field picked by a PropertyRef property of the same struct. The editor
// then draws it typed to whatever that PropertyRef points at — a drag field for
// a float, a checkbox for a bool, a dropdown for an enum — instead of raw text,
// and writes back the canonical text form (enums are stored as their index, so
// no device-side name table is needed).
//
//   DEKI_EXPORT PropertyRef target;
//   DEKI_EXPORT DEKI_VALUE_OF(target) std::string value;
#define DEKI_VALUE_OF(prop)
// Mark a float/Vector2 property as a physical quantity in meters. The inspector
// converts to pixels for display when the project's inspectorUnit is Pixels;
// internal storage is always meters. Valid arguments: Distance, Velocity, Acceleration.
#define DEKI_UNIT(unit)
// A reflected member's name IS its key in scene files (see COMPATIBILITY.md).
// When one has to be renamed, keep the old key loading:
//
//   DEKI_EXPORT DEKI_RENAMED_FROM("tint_color")
//   Color tintColor;
//
// The codegen emits the old key as an extra case of the same deserializer
// (device and editor) and an extra field-table row; the editor's JSON loader
// reads the old key when the new one is absent, reports it as a legacy key
// (`--verify-scenes` warns), and a save writes the new name. One alias per
// member; the alias must differ from the current identifier.
#define DEKI_RENAMED_FROM(oldName)
#endif

// ============================================================================
// Serializable struct markers (Unity-style automatic serialization)
// ============================================================================
// Place DEKI_SERIALIZABLE before a struct to auto-generate Serialize/Deserialize.
// All fields are serialized by default. Use DEKI_NO_SERIALIZE to exclude fields.
//
// Usage:
//   DEKI_SERIALIZABLE
//   struct MyData {
//       int32_t x;
//       std::string name;
//       DEKI_NO_SERIALIZE int32_t cached_value;  // Not serialized
//   };
//
#if defined(DEKI_REFLECT)
#define DEKI_SERIALIZABLE   [[=::Deki::deki_refl::serializable{}]]
#define DEKI_NO_SERIALIZE   [[=::Deki::deki_refl::no_serialize{}]]
#else
#define DEKI_SERIALIZABLE
#define DEKI_NO_SERIALIZE
#endif
#define DEKI_SERIALIZABLE_NAME(name)
#define DEKI_SERIALIZABLE_DEFAULT_VALUE(val)

/**
 * @brief Physical unit for float/Vector2 properties.
 *
 * Internal storage is always meters. Inspector applies a display conversion
 * based on the project's inspectorUnit setting:
 *   - inspectorUnit == Meters: display value == stored value (suffix m / m/s / m/s²).
 *   - inspectorUnit == Pixels: displayed value = stored * pixelsPerMeter (suffix px / px/s / px/s²).
 *
 * Only meaningful on Float and Vector2 property types.
 */
enum class PhysicalUnit : uint8_t {
    None = 0,          // Not a physical quantity (default)
    Distance,          // meters
    Velocity,          // meters per second
    Acceleration,      // meters per second squared
    Angle,             // radians (inspector displays degrees)
    AngularVelocity,   // radians per second (inspector displays degrees/s)
    Temperature        // degrees Celsius (inspector may display Fahrenheit)
};

/**
 * @brief Property types for runtime reflection
 */
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
    Asset,      // LEGACY: std::string GUID field (use AssetRef instead)
    Enum,
    ObjectRef,  // Reference to another Object by ID (uint32_t)
    Array,      // std::vector<T> - elementType specifies T's PropertyType
    AssetRef,   // AssetRef<T> - stores GUID + auto-loads asset pointer
    NodeRef,    // NodeRef - reference to another node-graph node by its graph id (uint32_t)
    PropertyRef // PropertyRef - a picked field of a component on an object (see PropertyRef.h)
};

/**
 * @brief Compact field descriptor — the ONE piece of reflection that exists on
 *        every platform, embedded included.
 *
 * PropertyInfo (below) is the editor's rich metadata: display names,
 * tooltips, ranges, enum value names. It is compiled only under DEKI_EDITOR and
 * costs far too much flash for a device build. But a device still needs to
 * answer "where does field X of component Y live, and what type is it" for
 * data-driven systems (a state machine writing a property, a tween driving a
 * field). That is exactly this struct: 8 bytes per field, in flash.
 *
 * Codegen emits one flattened table per concrete component (own + inherited
 * fields) and registers it with ComponentRegistry::RegisterFields on ALL
 * platforms. Look one up with ComponentRegistry::FindField(typeId, nameHash).
 */
struct FieldRef {
    uint32_t nameHash;   // HashString(field name)
    uint16_t offset;     // offsetof(Component, field)
    uint8_t  type;       // PropertyType
    uint8_t  enumSize;   // sizeof(enum) for Enum fields (1/2/4), else 0
};

/**
 * @brief Property metadata (stored per-component-type, not per-instance)
 *
 * This struct is designed to be stored in flash (static const) on ESP32-S3.
 * All string pointers should point to string literals.
 */
struct PropertyInfo {
    const char* name;           // Property name (matches field name)
    const char* displayName;    // Display name for editor (can be nullptr for auto)
    PropertyType type;      // Property type
    size_t offset;              // offsetof(Component, field)

    // Constraints
    const char* tooltip;        // Tooltip text (nullptr if none)
    float minValue;             // Min value for range (0 if no range)
    float maxValue;             // Max value for range (0 if no range)
    bool hasRange;              // True if minValue/maxValue are valid

    // Asset type (for Asset properties) — must match a typeName registered with
    // AssetTypeRegistry by the owning package
    const char* assetType;

    // Enum support. `enumValues` names each enumerator; `enumInts` (below,
    // trailing the struct) carries the UNDERLYING VALUE of each. A position in
    // these tables is NOT the number stored in the field: an enum may start
    // anywhere, skip values, or be a flags set (1, 2, 4, 8).
    const char* const* enumValues;  // Array of enum value names
    int enumCount;                  // Number of enum values
    uint8_t enumSize;               // sizeof(enum) - 1, 2, or 4 bytes

    // Array support (for Array properties)
    PropertyType elementType;   // Element type for Array (e.g., Int32, Float, String)

    // Visibility conditions (property is hidden unless condition is met)
    // If visibilityProperty is set, this property is only visible when:
    //   - If visibilityMask != 0: visible when bit (1 << value) is set in mask (exact match mode)
    //   - If visibilityMask == 0: visible when value >= visibilityMinValue (threshold mode)
    const char* visibilityProperty; // Name of property that controls visibility (nullptr = always visible)
    int visibilityMinValue;         // Minimum value for threshold mode (visibilityMask == 0)
    uint32_t visibilityMask;        // Bitmask of allowed values (0 = use threshold mode)

    // Object-picker support. On an ObjectRef property: the component type the
    // picker filters by (from ObjectRef<T>). On a String property: set by
    // DEKI_OBJECT_NAME, marking the field as an object NAME — non-null enables
    // the picker, "" means no component filter.
    const char* componentRefType;

    // Editor customization flags (used by inspector)
    bool isMultiline;               // Use multiline text editor for String type
    bool triggersAssetRegen;        // Changing this property should regenerate associated assets
    const char* assetRegenType;     // Type of regeneration: "font", "texture", etc.
    bool useSlider;                 // Use slider UI instead of input field (for numeric types with range)

    // Group/section support (for inspector UI organization)
    const char* group;              // Group name for section header (nullptr = no group)

    // Toggle object active state (for bool properties that control ObjectRef visibility)
    const char* togglesObjectOf;    // ObjectRef field name whose object's active state this bool controls (nullptr = none)

    // Physical unit metadata (for inspector-side display conversion).
    // Trails the struct so existing .gen.cpp initializers remain valid (default-inits to None).
    // Only meaningful on Float and Vector2 properties.
    PhysicalUnit physicalUnit = PhysicalUnit::None;

    // DEKI_VALUE_OF: name of the PropertyRef property this String property is
    // the literal value for. The editor types the value control from whatever
    // that reference points at. Trails the struct (append-only rule).
    const char* valueOfProperty = nullptr;

    // DEKI_RENAMED_FROM: the key this property was serialised under before it
    // was renamed; the JSON loader reads it when the current name is absent.
    // Trails the struct (append-only rule); the reflection ABI handshake's
    // sizeofPropertyInfo refuses packages built without it.
    const char* renamedFrom = nullptr;

    // Underlying value of each enumerator, parallel to `enumValues` and the
    // same `enumCount` long. Authored JSON stores the NAME and resolves it back
    // through this table, so reordering an enum no longer rewrites the meaning
    // of saved files. Anything needing the integer must look it up here: the
    // index into these tables is not the stored value whenever an enum starts
    // off zero, leaves a gap, or is a flags set.
    // Trails the struct (append-only rule); null on a package built before it,
    // which the handshake's sizeofPropertyInfo already refuses.
    const int32_t* enumInts = nullptr;
};

/// Name of the enumerator whose underlying value is `value`, or nullptr when
/// the enum declares no such value. Needs `enumInts`, so it answers nullptr on
/// a property whose package predates that table.
inline const char* EnumNameForValue(const PropertyInfo& prop, int32_t value)
{
    if (!prop.enumValues || !prop.enumInts) return nullptr;
    for (int i = 0; i < prop.enumCount; ++i)
        if (prop.enumInts[i] == value) return prop.enumValues[i];
    return nullptr;
}

/// Underlying value of the enumerator called `name`. False when the enum has no
/// enumerator by that name, leaving `out` untouched.
inline bool EnumValueForName(const PropertyInfo& prop, const char* name, int32_t& out)
{
    if (!prop.enumValues || !prop.enumInts || !name) return false;
    for (int i = 0; i < prop.enumCount; ++i)
    {
        const char* candidate = prop.enumValues[i];
        if (!candidate) continue;
        const char* a = candidate;
        const char* b = name;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a == 0 && *b == 0) { out = prop.enumInts[i]; return true; }
    }
    return false;
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

/**
 * @brief Component metadata for runtime reflection
 */
struct ComponentMeta {
    const char* name;           // The identifier alone ("Health"): display, and the scene file's "class" hint
    const char* serializedName; // The fully qualified C++ name ("Game::Health"): the scene file's "type" key
    const char* category;       // Category (e.g., "2D", "Rendering")
    const char* description;    // Description text

    uint32_t typeId;            // TypeId<T>() == HashString(serializedName)
    uint32_t baseTypeId;        // Base class ComponentType

    bool isAbstract;            // True if component cannot be instantiated
    bool isUnique;              // True if only one per object allowed
    bool bootSceneOnly;        // True if component can only be added to boot scenes
    bool platformSceneOnly;    // True if component can only be added to platform scenes

    size_t instanceSize;        // sizeof(Component)

    // Factory function (editor only - can be nullptr for runtime)
    Component* (*createFunc)();

    // Properties
    const PropertyInfo* properties;
    int propertyCount;

    // Display name buffer - auto-generated from class name (CamelCase → "Camel Case")
    mutable char displayNameBuffer[64];
    mutable bool displayNameGenerated;

    // Explicit display name override (DEKI_DISPLAY_NAME("...") above the class).
    // When non-null, GetDisplayName() returns this verbatim instead of auto-generating.
    // Trails the struct so existing .gen.cpp initializers remain valid (default-inits to nullptr).
    const char* displayNameOverride = nullptr;

    // DEKI_SETTINGS_SECTION: the Project Settings section this type provides;
    // nullptr for an ordinary scene component. Trails the struct (append-only rule).
    const char* settingsSection = nullptr;

    // The runtime entry for this class (factory, deserializer, field table).
    // Trails the struct (append-only rule).
    const ComponentTypeInfo* typeInfo = nullptr;

    // DEKI_FORMER_NAME: qualified names this component used to be saved under,
    // newest first. A scene stores the qualified class name, so without these a
    // rename, or a move into a namespace, orphans every instance in every saved
    // scene. The loader resolves an unrecognised type through this table and
    // writes the current name back on save.
    // Trails the struct (append-only rule).
    const char* const* formerNames = nullptr;
    int formerNameCount = 0;

    // Get the display name. Prefers an explicit override; otherwise derives from `name`.
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
            // Add space before uppercase letters (except at start)
            if (dst != displayNameBuffer && *src >= 'A' && *src <= 'Z') {
                // Check if previous char was lowercase or if next char is lowercase
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
// Property access helpers (inline for zero overhead)
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

// Node property access: node-graph nodes are NOT Component, so the node
// canvas reads/writes their fields through a void* base + offset (mirrors
// GetPropertyRef but decoupled from the component base class).
template<typename T>
inline T& GetNodePropertyRef(void* base, const PropertyInfo& prop) {
    return *reinterpret_cast<T*>(reinterpret_cast<char*>(base) + prop.offset);
}

template<typename T>
inline const T& GetNodePropertyRef(const void* base, const PropertyInfo& prop) {
    return *reinterpret_cast<const T*>(reinterpret_cast<const char*>(base) + prop.offset);
}

// ============================================================================
// Component metadata markers (optional, ABOVE the class)
// ============================================================================
// A component is any class deriving publicly from Deki::Component. The
// reflection codegen finds it by that alone: no macro in
// the class, no generated include, and its identity is its qualified C++ name
// (see TypeId.h). These markers add editor metadata. They sit on the lines right
// above the class and attach to the next component declared in the same file:
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
// DEKI_DESCRIPTION   One line on what the component DOES, shown under its name
//                    in the picker. Write it for a reader who has never opened
//                    the class.
// DEKI_DISPLAY_NAME  Inspector name when the one derived from the class name
//                    ("DS3231RTCComponent" -> "DS3231 RTC") is not right.
// DEKI_SETTINGS_SECTION  Makes the component a Project Settings section of
//                    that name instead of a scene component.
// DEKI_FORMER_NAME   A qualified name this component used to have. Scenes
//                    store the qualified class name, so renaming a class or
//                    moving it into a namespace would otherwise orphan every
//                    component in every saved scene. Add one line per previous
//                    name, oldest last, and old scenes keep loading and are
//                    written back under the current name on save:
//
//                        DEKI_FORMER_NAME("SpriteComponent")
//                        class Deki2D::SpriteComponent : public RendererComponent
//
//                    Never delete one: the file that still uses it may be
//                    older than any migration you remember. The name must stay
//                    unique across the project, and the loader says so if two
//                    components claim the same one.
//
// Only the reflection generator sees them; every other build expands them to
// nothing. Exported fields must be public: the generated code reaches them
// from outside the class.
#if defined(DEKI_REFLECT)
namespace deki_refl {
    struct category_tag { const char* text; };
    struct description_tag { const char* text; };
    struct display_name_tag { const char* text; };
    struct settings_section_tag { const char* text; };
    struct former_name_tag { const char* text; };  // DEKI_FORMER_NAME("Old::Name")
}
#define DEKI_REFL_CONCAT2(a, b) a##b
#define DEKI_REFL_CONCAT(a, b) DEKI_REFL_CONCAT2(a, b)
#define DEKI_REFL_TAG(Tag, text) \
    inline constexpr ::Deki::deki_refl::Tag DEKI_REFL_CONCAT(deki_refl_tag_, __COUNTER__){ std::define_static_string(text) };
#define DEKI_CATEGORY(text)         DEKI_REFL_TAG(category_tag, text)
#define DEKI_DESCRIPTION(text)      DEKI_REFL_TAG(description_tag, text)
#define DEKI_DISPLAY_NAME(text)     DEKI_REFL_TAG(display_name_tag, text)
#define DEKI_SETTINGS_SECTION(text) DEKI_REFL_TAG(settings_section_tag, text)
#define DEKI_FORMER_NAME(text)      DEKI_REFL_TAG(former_name_tag, text)
#else
#define DEKI_CATEGORY(text)
#define DEKI_DESCRIPTION(text)
#define DEKI_DISPLAY_NAME(text)
#define DEKI_SETTINGS_SECTION(text)
#define DEKI_FORMER_NAME(text)
#endif

// ============================================================================
// Node declaration macro (standalone node-graph tool; NOT a component)
// ============================================================================
// Place DEKI_NODE inside a node struct body and DEKI_EXPORT above each field,
// then include the generated .gen.h at the end of the header. New node types come
// from package/project DLLs (hot-reloadable), so the editor's node canvas draws
// them generically with no editor rebuild. Node structs have no base class and no
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
// Category convention: "Domain/MenuGroup". The first path segment is the
// graph-domain scoping key (matched against DekiNodeGraphDomain::domainKey);
// the remainder groups the add-node context menu.
//
// StaticType is intentionally NOT declared here (that would require the string
// hash in the node header); the codegen computes HashString(StaticNodeName)
// in the generated .gen.cpp, keeping node headers decoupled from Component.
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

// Optional pin declarations. Labels are editor-only display metadata; runtime
// semantics come from pin *indices* in the graph's link table (interpreter-defined).
// Pass at least one label; a node with zero pins in a direction simply omits
// the macro. (No __VA_OPT__: the editor builds as C++17.)
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

// Output pin count driven at edit time by a DEKI_EXPORT property of this node:
// Int32 property => count = value; Array property => count = element count.
// Mutually exclusive with DEKI_NODE_OUTPUTS. When the property is an Array of
// String, the editor uses the string values as the output pin labels (e.g. an
// FSM state's transition event names); otherwise pins are labeled by index.
#define DEKI_NODE_DYNAMIC_OUTPUTS(PropName) \
    public: \
        static constexpr const char* StaticDynamicOutputsProperty = PropName;

// Declare that this node owns an ordered list of child node instances, all of
// whose types carry the given full category (e.g. "Fsm/Actions"). Children are
// authored as a stack in the node's inspector (PlayMaker-style action lists),
// serialized inside the parent's graph entry ({enabled, type, values} each),
// and loaded at runtime into NodeGraphData::NodeInstance::children. Node types
// matching a child category never appear in the canvas add-node menu — they
// exist only inside a parent's stack.
#define DEKI_NODE_CHILDREN(Category) \
    public: \
        static constexpr const char* StaticNodeChildCategory = Category;

// Declare that this node CONTAINS an inner graph rather than a flat child
// stack: double-clicking it on the canvas descends into that graph, and the
// breadcrumb walks back out. `ContentCategory` is the full category whose node
// types the inner add-node menu offers (e.g. "Fsm/Actions" for a state's action
// flow); `EntryTypeName` is the StaticNodeName of the one node auto-seeded
// inside every instance's graph as its entry point (pass "" for none).
//
// The inner graph is a full graph: its own nodes, links and pin semantics, all
// interpreted by the consuming tool exactly like the root. Node ids are unique
// across the WHOLE document, so a node id still identifies one node no matter
// how deep it sits.
//
// Mutually exclusive with DEKI_NODE_CHILDREN: a node either stacks children in
// its inspector or owns a graph, never both.
#define DEKI_NODE_SUBGRAPH(ContentCategory, EntryTypeName) \
    public: \
        static constexpr const char* StaticNodeSubgraphCategory = ContentCategory; \
        static constexpr const char* StaticNodeSubgraphEntry = EntryTypeName;

// Use a DEKI_EXPORT String property of this node as its canvas title (falls
// back to the display name while the property is empty). Lets instances carry
// author-given names ("Attack", "Patrol") instead of N copies of the type name.
#define DEKI_NODE_TITLE_PROPERTY(PropName) \
    public: \
        static constexpr const char* StaticNodeTitleProperty = PropName;

// Mark this node type as a FIXED PART of every graph of its domain (e.g. the
// FSM's Awake/Start/Update lifecycle entries): the domain's default content
// seeds it, the editor ensures it exists when opening a graph, and it can
// neither be added from the canvas menu nor deleted. Wire its outputs or
// leave them unused — like a lifecycle hook you didn't override.
#define DEKI_NODE_PERMANENT() \
    public: \
        static constexpr bool StaticNodePermanent = true;

// Declare that this node's child stack (DEKI_NODE_CHILDREN) holds the graph's
// VARIABLES rather than ordinary children. The convention each child type
// follows: its DEKI_NODE_TITLE_PROPERTY is the variable's name, and its other
// exported property is the initial value — whose reflected type IS the
// variable's type. So one child struct per type:
//
//   struct MyNumberVariable {
//       DEKI_NODE(MyNumberVariable, "MyNumberVar", "MyDomain/Variables")
//       DEKI_NODE_TITLE_PROPERTY("name")
//       DEKI_EXPORT std::string name = "value";
//       DEKI_EXPORT float initial = 0.0f;
//   };
//
// The editor then offers those names wherever a PropertyRef can point at a
// variable, and the runner allocates live storage per instance. Pair with
// DEKI_NODE_PERMANENT so every graph of the domain has exactly one.
#define DEKI_NODE_VARIABLES() \
    public: \
        static constexpr bool StaticNodeVariables = true;

// For one line on what the node does, declare `static constexpr const char*
// StaticNodeDescription = "Waits until the clock is inside the window.";`
// beside the display name. The add picker shows it under the name, which is
// what tells two similarly named nodes apart before you add the wrong one.
//
// For a human-readable editor label (add-node menu + canvas title), declare a
// plain member in the node struct: `static constexpr const char*
// StaticNodeDisplayName = "Time Window";`. The codegen picks it up into
// DekiNodeMeta::displayName; without it the editor nicifies the node name. The
// node name (StaticNodeName) stays the stable serialization id.

}  // namespace Deki
