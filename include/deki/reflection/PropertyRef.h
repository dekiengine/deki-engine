#pragma once

#include <cstdint>
#include <string>

#include <deki/reflection/Property.h>  // DekiFieldRef, DekiPropertyType

namespace Deki
{


class Object;
class Component;

/**
 * @brief The object's own transform, addressable like a component.
 *
 * The transform is not a component: it lives on Object with private fields
 * and setters that invalidate the cached world transform of the object and all
 * its descendants. So a Transform-targeted reference is DISPATCHED by id rather
 * than written through an offset, and every write goes through the real setter.
 *
 * Position and Scale address both axes at once (a Vector2-typed target), which
 * is what makes "move this object to (x, y)" a single generic action instead of
 * two that would run one after the other.
 */
enum class TransformField : uint8_t
{
    None = 0,
    X,
    Y,
    Position,
    Rotation,
    ScaleX,
    ScaleY,
    Scale,
    Active,
    // The third axis, appended so the values above keep their numbers.
    Z,
    RotationX,
    RotationY,
    ScaleZ,
};

/** @brief The component name a reference stores to mean "the object's transform". */
inline constexpr const char* kTransformRefComponent = "Transform";

/**
 * @brief The component name a reference stores to mean "a variable of this graph".
 *
 * Variables are declared by a node carrying DEKI_NODE_VARIABLES and live in
 * storage the graph's RUNNER allocates per instance, so the engine cannot
 * resolve them: BindPropertyRef leaves these alone and the runner binds them
 * itself with DekiVariableFieldRef. Field = the variable's name.
 */
inline constexpr const char* kVariableRefComponent = "Variable";

/**
 * @brief A field descriptor for a standalone value of `type`, at offset 0.
 *
 * For runners binding their own storage (graph variables): point
 * PropertyBinding::field at the value and its `info` at this, and every
 * Read/Write/Compare helper below works on it unchanged. Returns nullptr for a
 * type that has no literal form.
 */
const FieldRef* DekiVariableFieldRef(PropertyType type);

/**
 * @brief The transform's field table, for pickers and binding.
 *
 * NOTE: in THIS table only, FieldRef::offset carries a TransformField
 * id, not a byte offset (the fields are private and dispatched, see above).
 * Nothing outside BindPropertyRef should read it as an offset.
 */
const FieldRef* TransformFields(int& outCount);

/**
 * @brief Transform field names, index-aligned with TransformFields.
 *
 * FieldRef stores only a name hash (that is the point: 8 bytes per field),
 * so the names live here for the editor's pickers and for error messages.
 */
const char* const* TransformFieldNames(int& outCount);

/**
 * @brief A picked "field of a component on an object".
 *
 * The editor authors one of these with three cascading dropdowns (object ->
 * component -> field), so an invalid reference cannot be authored at all. What
 * gets stored is ids, not prose: the component is identified by the same type
 * GUID scenes use, the field by its name. Both are hashed ONCE at load
 * (Rehash), after which resolving costs a small linear scan and never touches a
 * string again.
 *
 * The object stays a NAME on purpose. Object ids are per-scene, and a graph
 * asset is shared across every scene that uses it, so a name is the only
 * identity that survives. "" means "the object this component is on".
 *
 * Resolve once (Awake, state entry) into a PropertyBinding and reuse it; do not
 * resolve per frame.
 */
struct PropertyRef
{
    // ---- Authored / serialized ----
    std::string object;     // object name in the scene; "" = own object
    std::string component;  // component type GUID (::Deki::TypeName<Component>())
    std::string field;      // field name as declared in C++

    // ---- Derived at load (Rehash), never serialized ----
    uint32_t componentTypeId = 0;  // HashString(component)
    uint32_t fieldHash = 0;        // HashString(field)

    // Recompute the ids from the authored strings. Called by the generated
    // Deserialize and by the editor whenever the reference is edited.
    void Rehash();

    bool IsSet() const { return componentTypeId != 0 && fieldHash != 0; }
};

/**
 * @brief A resolved PropertyRef: where the field lives and what it is.
 *
 * Plain old data on purpose - callers cache it in zero-initialized scratch
 * memory (e.g. an FSM action's per-run state blob), so it must survive being
 * memset to zero and must not own anything.
 */
struct PropertyBinding
{
    void* field = nullptr;  // component field (null for transform targets)
    const FieldRef* info = nullptr;
    // Literal operand pre-parsed for this field's type (see ParsePropertyLiteral).
    // Unused for String fields, which read their text straight from the caller.
    double number = 0.0;
    double number2 = 0.0;  // second axis, for Vector2-typed targets

    // Transform targets go through Object's setters instead of `field`.
    Object* object = nullptr;
    TransformField transform = TransformField::None;

    bool IsBound() const { return info != nullptr && (field != nullptr || object != nullptr); }
    bool IsTransform() const { return transform != TransformField::None; }
};

/**
 * @brief Resolve `ref` against `object` (already found by name by the caller).
 *
 * Fills out.field / out.info. Returns false when the object carries no such
 * component or the component has no such field; `outError` (optional, may be
 * null) then receives a short human-readable reason for the caller to log.
 * Does not touch out.number.
 */
bool BindPropertyRef(Object* object, const PropertyRef& ref, PropertyBinding& out,
                     const char** outError = nullptr);

/**
 * @brief Parse an authored literal per the bound field's type.
 *
 * Numbers, bools ("true"/"1") and enums (stored as their index - the editor
 * resolves value names, the device never needs the name table) land in `out`.
 * Vector2-typed targets read "x, y" and fill `out2` as well. String fields need
 * no parsing and return true with both untouched. Returns false when the text
 * is not a number for a numeric field.
 */
bool ParsePropertyLiteral(const FieldRef& info, const char* text, double& out, double& out2);

/** @brief Single-value overload for targets that are not Vector2-typed. */
bool ParsePropertyLiteral(const FieldRef& info, const char* text, double& out);

/** @brief Read the bound target's live value (first axis for Vector2). */
double ReadBoundProperty(const PropertyBinding& binding);

/** @brief Read the bound target's second axis (Vector2 targets only, else 0). */
double ReadBoundProperty2(const PropertyBinding& binding);

/** @brief Write raw numbers to the bound target, bypassing the parsed literal. */
void WriteBoundNumbers(const PropertyBinding& binding, double v, double v2);

/** @brief Write the bound literal (or `text` for String fields) into the field. */
void WriteBoundProperty(const PropertyBinding& binding, const std::string& text);

/**
 * @brief Compare the field's live value against the bound literal.
 * @return <0, 0 or >0 like strcmp. String fields compare against `text`.
 */
int CompareBoundProperty(const PropertyBinding& binding, const std::string& text);

}  // namespace Deki
