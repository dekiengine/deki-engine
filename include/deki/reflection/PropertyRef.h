#pragma once

#include <cstdint>
#include <string>

#include <deki/reflection/Property.h>  // FieldRef, PropertyType

namespace Deki
{

class Object;
class Component;

/// A field of the object's own transform, addressable like a component field.
///
/// The transform is not a component: it lives on Object, with private fields
/// and setters that invalidate the cached world transform of the object and
/// its descendants. So a reference to it is dispatched by this id, not written
/// through an offset, and every write goes through the setter.
///
/// Position and Scale cover both axes at once (a Vector2 target), so "move
/// this object to (x, y)" is one action, not two.
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
    // The third axis. Append only, so the values above keep their numbers.
    Z,
    RotationX,
    RotationY,
    ScaleZ,
};

/// The component name a reference stores to mean "the object's transform".
inline constexpr const char* kTransformRefComponent = "Transform";

/// The component name a reference stores to mean "a variable of this graph".
/// The field is the variable's name.
///
/// Variables are declared by a node with DEKI_NODE_VARIABLES and live in
/// storage the graph's runner allocates per instance. BindPropertyRef skips
/// them; the runner binds them itself with DekiVariableFieldRef.
inline constexpr const char* kVariableRefComponent = "Variable";

/// A field descriptor for a standalone value of `type`, at offset 0, or
/// nullptr for a type with no literal form.
///
/// For runners binding their own storage (graph variables): point
/// PropertyBinding::field at the value and its `info` at this, and the
/// Read/Write/Compare helpers below work on it.
const FieldRef* DekiVariableFieldRef(PropertyType type);

/// The transform's field table, for pickers and binding.
///
/// In this table only, FieldRef::offset holds a TransformField id, not a byte
/// offset, because the fields are private and dispatched (see above). Only
/// BindPropertyRef reads it.
const FieldRef* TransformFields(int& outCount);

/// Transform field names, in the same order as TransformFields. FieldRef
/// stores only a name hash, so the names for pickers and error messages live
/// here.
const char* const* TransformFieldNames(int& outCount);

/// A picked field of a component on an object.
///
/// The editor sets one with three dropdowns (object, then component, then
/// field), so only valid references can be made. The component is stored by
/// the qualified class name scenes use, the field by its name. Both are hashed
/// once at load (Rehash); resolving is then a small linear scan with no string
/// work.
///
/// The object is a name, because object ids are per scene and a graph asset is
/// shared by every scene that uses it. "" means the object this component is
/// on.
///
/// Resolve once (Awake, state entry) into a PropertyBinding and reuse it, not
/// every frame.
struct PropertyRef
{
    // ---- Authored and serialized ----
    std::string object;     // object name in the scene; "" = own object
    std::string component;  // qualified class name (::Deki::TypeName<Component>())
    std::string field;      // field name as declared in C++

    // ---- Derived at load (Rehash), never serialized ----
    uint32_t componentTypeId = 0;  // HashString(component)
    uint32_t fieldHash = 0;        // HashString(field)

    // Recomputes the ids from the strings. Called by the generated Deserialize
    // and by the editor whenever the reference is edited.
    void Rehash();

    bool IsSet() const { return componentTypeId != 0 && fieldHash != 0; }
};

/// A resolved PropertyRef: where the field lives and what it is.
///
/// Must stay plain old data: callers keep it in zero-initialized scratch
/// memory (e.g. an FSM action's per-run state), so it must survive a memset
/// to zero and own nothing.
struct PropertyBinding
{
    void* field = nullptr;  // component field (null for transform targets)
    const FieldRef* info = nullptr;
    // Literal operand parsed for this field's type (see ParsePropertyLiteral).
    // Unused for String fields, which take their text from the caller.
    double number = 0.0;
    double number2 = 0.0;  // second axis, for Vector2-typed targets

    // Transform targets go through Object's setters instead of `field`.
    Object* object = nullptr;
    TransformField transform = TransformField::None;

    bool IsBound() const { return info != nullptr && (field != nullptr || object != nullptr); }
    bool IsTransform() const { return transform != TransformField::None; }
};

/// Resolves `ref` on `object`, which the caller has already found by name.
/// Fills out.field and out.info, and leaves out.number as it is.
///
/// Returns false when the object has no such component or the component no
/// such field. `outError`, when not null, then gets a short reason to log.
bool BindPropertyRef(Object* object, const PropertyRef& ref, PropertyBinding& out, const char** outError = nullptr);

/// Parses a literal by the bound field's type.
///
/// Numbers, bools ("true"/"1") and enums go into `out`. Enums are stored as
/// their index, so the device needs no name table. Vector2 targets read
/// "x, y" and also fill `out2`. String fields return true and leave both
/// alone. Returns false when a numeric field's text is not a number.
bool ParsePropertyLiteral(const FieldRef& info, const char* text, double& out, double& out2);

/// For targets that are not Vector2.
bool ParsePropertyLiteral(const FieldRef& info, const char* text, double& out);

/// The bound target's current value (the first axis for Vector2).
double ReadBoundProperty(const PropertyBinding& binding);

/// The bound target's second axis, or 0 when it is not a Vector2.
double ReadBoundProperty2(const PropertyBinding& binding);

/// Writes `v` and `v2` to the bound target instead of the parsed literal.
void WriteBoundNumbers(const PropertyBinding& binding, double v, double v2);

/// Writes the parsed literal, or `text` for String fields, into the field.
void WriteBoundProperty(const PropertyBinding& binding, const std::string& text);

/// Compares the field's current value with the parsed literal, or with `text`
/// for String fields. Returns <0, 0 or >0 like strcmp.
int CompareBoundProperty(const PropertyBinding& binding, const std::string& text);

}  // namespace Deki
