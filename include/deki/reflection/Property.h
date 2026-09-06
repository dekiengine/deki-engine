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

struct DekiNodeMeta;   // node-graph node metadata (full definition in deki-nodegraph/DekiNode.h), a package type: global

namespace Deki
{

class Component;
class Scene;  // ResolveObjectRefs(Scene*) in the DEKI_COMPONENT macros

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

// Mark a Component subclass as a project-settings type and assign it a
// section name (e.g. "Rendering", "Audio") in the editor's Project Settings
// panel. Use inside the class body. The SettingsRegistry reads
// ClassName::StaticSettingsSection at registration time.
//
// Usage:
//   class RenderingProjectSettings : public Component {
//       DEKI_COMPONENT(RenderingProjectSettings, Component, "Settings",
//                      "<guid>", "")
//       DEKI_PROJECT_SETTINGS_SECTION("Rendering")
//   public:
//       DEKI_TOOLTIP("...")
//       DEKI_EXPORT bool half_width_framebuffer = false;
//   };
//
// Pair with DEKI_REGISTER_PROJECT_SETTINGS / DEKI_REGISTER_PROJECT_SETTINGS_PLUGIN
// in the .cpp to actually register with the registry.
#define DEKI_PROJECT_SETTINGS_SECTION(SectionName) \
    public: static constexpr const char* StaticSettingsSection = SectionName;

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
    None = 0,        // Not a physical quantity (default)
    Distance,        // meters
    Velocity,        // meters per second
    Acceleration,    // meters per second squared
    Angle            // radians (inspector displays degrees)
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

    // Enum support
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
};

/**
 * @brief Component metadata for runtime reflection
 */
struct ComponentMeta {
    const char* name;           // Class name for display (the C++ class name)
    const char* serializedName; // Stable GUID for scene serialization (e.g., "a1b2c3d4-e5f6-7890-abcd-ef0123456789")
    const char* category;       // Category (e.g., "2D", "Rendering")
    const char* description;    // Description text

    uint32_t typeId;            // ComponentType from DECLARE_COMPONENT_TYPE
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

    // Explicit display name override (set via DEKI_DISPLAY_NAME("...") in the class).
    // When non-null, GetDisplayName() returns this verbatim instead of auto-generating.
    // Trails the struct so existing .gen.cpp initializers remain valid (default-inits to nullptr).
    const char* displayNameOverride = nullptr;

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
// Unified component declaration macro
// ============================================================================
// Use DEKI_COMPONENT inside the class declaration.
// Use DEKI_EXPORT above each field to expose in editor.
// Include the generated .gen.h at end of header.
//
// Usage:
//   class MyComponent : public Behaviour
//   {
//       DEKI_COMPONENT(MyComponent, Behaviour, "2D", "your-guid-here", "DEKI_FEATURE_MY")
//   public:
//       DEKI_EXPORT
//       int myProperty;
//   };
//   #include "generated/MyComponent.gen.h"
//

// ============================================================================
// Reflection method declarations (editor-only)
// ============================================================================
// These methods are implemented in generated .gen.cpp files.
// On embedded platforms, reflection is not needed - only Deserialize() is used.

#ifdef DEKI_EDITOR
    #define DEKI_REFLECTION_DECLARATIONS() \
        static const ::Deki::PropertyInfo* GetProperties(); \
        static int GetPropertyCount(); \
        static const ::Deki::ComponentMeta& GetComponentMeta();
#else
    #define DEKI_REFLECTION_DECLARATIONS() /* No reflection on embedded */
#endif

// GetFields/GetFieldCount are NOT inside DEKI_REFLECTION_DECLARATIONS: the
// compact field table is emitted for every platform (see FieldRef), unlike
// the editor-only rich metadata.
#define DEKI_COMPONENT_BASE(BaseTypeName, Category) \
        static constexpr ::Deki::ComponentType BaseStaticType = BaseTypeName::StaticType; \
        static constexpr const char* ComponentCategory = Category; \
        ::Deki::ComponentType GetType() const override { return StaticType; } \
        ::Deki::ComponentType GetBaseType() const override { return BaseStaticType; } \
        bool Deserialize(::Deki::SceneFormat::SceneMsgPackParser& parser, uint32_t mapSize) override; \
        void ResolveObjectRefs(::Deki::Scene* scene) override; \
        void RemapObjectRefs(const std::unordered_map<uint32_t, uint32_t>& idRemap) override; \
        bool DeclaresUpdate() const override; \
        static const ::Deki::FieldRef* GetFields(); \
        static int GetFieldCount(); \
        DEKI_REFLECTION_DECLARATIONS()

#define DEKI_COMPONENT(TypeName, BaseTypeName, Category, TypeGuid, FeatureDefine) \
    public: \
        static constexpr ::Deki::ComponentType StaticType = ::Deki::HashString(TypeGuid); \
        static constexpr const char* StaticGuid = TypeGuid; \
        static constexpr const char* StaticFeatureDefine = FeatureDefine; \
        DEKI_COMPONENT_BASE(BaseTypeName, Category)

// Optional: declare an explicit inspector display name for this component.
// Place inside the class body. The metadata generator picks it up and wires it
// into the component's ComponentMeta::displayNameOverride; runtime
// GetDisplayName() returns this string verbatim instead of deriving from the class name.
//
//   class DS3231RTCComponent : public SetupComponent {
//       DEKI_COMPONENT(DS3231RTCComponent, SetupComponent, "Sensors", "<guid>", "")
//       DEKI_DISPLAY_NAME("DS3231 RTC")
//       ...
//   };
//
// Expands to a static constexpr string the generator can also reference directly.
#define DEKI_DISPLAY_NAME(name) \
    public: \
        static constexpr const char* StaticDisplayName = name;

// Optional: one line on what this component DOES, shown under its name in the
// Add Component picker. Write it for a reader who has never opened the class:
// a project accumulates a dozen components with names like "HeroController"
// and "LivingController", and the name alone does not say which is which.
//
//   class ScrollComponent : public Behaviour {
//       DEKI_COMPONENT(ScrollComponent, Behaviour, "2D", "<guid>", "")
//       DEKI_DESCRIPTION("Drag-scrolls its children with momentum and clamping.")
//       ...
//   };
#define DEKI_DESCRIPTION(text) \
    public: \
        static constexpr const char* StaticDescription = text;

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
        static const DekiNodeMeta& GetNodeMeta();
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
