#include <deki/reflection/PropertyRef.h>

#include <deki/reflection/ComponentRegistry.h>
#include <deki/Component.h>  // HashString
#include "reflection/LegacyTypeNames.h"
#include <deki/Object.h>

#include <cstdlib>
#include <cstring>

namespace Deki
{


void PropertyRef::Rehash()
{
#ifdef DEKI_EDITOR
    // Authored before 0.11: the component is a GUID. Store the class name so
    // the next save writes it.
    if (const char* legacyName = LegacyComponentTypeName(component))
        component = legacyName;
#endif
    componentTypeId = component.empty() ? 0u : HashString(component.c_str());
    fieldHash = field.empty() ? 0u : HashString(field.c_str());
}

// The transform table. `offset` carries a TransformField id here, NOT a byte
// offset: the fields are private on Object and every write must go through
// the setter that invalidates the world-transform cache.
//
// Position and Scale are Vector2-typed so both axes move in ONE action; the
// single-axis entries are there for when only one should change.
namespace
{
const FieldRef kTransformFields[] = {
    { HashString("position"), (uint16_t)TransformField::Position, (uint8_t)PropertyType::Vector2, 0 },
    { HashString("x"), (uint16_t)TransformField::X, (uint8_t)PropertyType::Float, 0 },
    { HashString("y"), (uint16_t)TransformField::Y, (uint8_t)PropertyType::Float, 0 },
    { HashString("rotation"), (uint16_t)TransformField::Rotation, (uint8_t)PropertyType::Float, 0 },
    { HashString("scale"), (uint16_t)TransformField::Scale, (uint8_t)PropertyType::Vector2, 0 },
    { HashString("scaleX"), (uint16_t)TransformField::ScaleX, (uint8_t)PropertyType::Float, 0 },
    { HashString("scaleY"), (uint16_t)TransformField::ScaleY, (uint8_t)PropertyType::Float, 0 },
    { HashString("active"), (uint16_t)TransformField::Active, (uint8_t)PropertyType::Bool, 0 },
};

const char* const kTransformFieldNames[] = {
    "position",
    "x",
    "y",
    "rotation",
    "scale",
    "scaleX",
    "scaleY",
    "active",
};

static_assert(sizeof(kTransformFields) / sizeof(kTransformFields[0]) ==
                  sizeof(kTransformFieldNames) / sizeof(kTransformFieldNames[0]),
              "transform field names must stay index-aligned with the field table");
}  // namespace

const FieldRef* TransformFields(int& outCount)
{
    outCount = static_cast<int>(sizeof(kTransformFields) / sizeof(kTransformFields[0]));
    return kTransformFields;
}

const char* const* TransformFieldNames(int& outCount)
{
    outCount = static_cast<int>(sizeof(kTransformFieldNames) / sizeof(kTransformFieldNames[0]));
    return kTransformFieldNames;
}

const FieldRef* DekiVariableFieldRef(PropertyType type)
{
    // Standalone values: offset 0, the caller's pointer IS the value.
    static const FieldRef kFloat{ 0, 0, (uint8_t)PropertyType::Float, 0 };
    static const FieldRef kBool{ 0, 0, (uint8_t)PropertyType::Bool, 0 };
    static const FieldRef kString{ 0, 0, (uint8_t)PropertyType::String, 0 };
    static const FieldRef kInt32{ 0, 0, (uint8_t)PropertyType::Int32, 0 };
    static const FieldRef kVector2{ 0, 0, (uint8_t)PropertyType::Vector2, 0 };

    switch (type)
    {
        case PropertyType::Float: return &kFloat;
        case PropertyType::Double: return &kFloat;  // stored as float
        case PropertyType::Bool: return &kBool;
        case PropertyType::String: return &kString;
        case PropertyType::Int32: return &kInt32;
        case PropertyType::Vector2: return &kVector2;
        default: return nullptr;
    }
}

namespace
{
bool BindTransform(Object* object, uint32_t fieldHash, PropertyBinding& out,
                   const char** outError)
{
    int count = 0;
    const FieldRef* fields = TransformFields(count);
    for (int i = 0; i < count; ++i)
    {
        if (fields[i].nameHash != fieldHash)
            continue;
        out.object = object;
        out.transform = static_cast<TransformField>(fields[i].offset);
        out.info = &fields[i];
        out.field = nullptr;
        return true;
    }
    if (outError) *outError = "the transform has no such field";
    return false;
}
}  // namespace

bool BindPropertyRef(Object* object, const PropertyRef& ref, PropertyBinding& out,
                     const char** outError)
{
    if (!object)
    {
        if (outError) *outError = "no target object";
        return false;
    }
    if (!ref.IsSet())
    {
        if (outError) *outError = "reference has no component/field picked";
        return false;
    }

    // The object's own transform, addressed like a component.
    if (ref.componentTypeId == HashString(kTransformRefComponent))
        return BindTransform(object, ref.fieldHash, out, outError);

    Component* comp = object->GetComponentByTypeId(ref.componentTypeId);
    if (!comp)
    {
        if (outError) *outError = "object has no such component";
        return false;
    }

    const FieldRef* info = ComponentRegistry::Instance().FindField(ref.componentTypeId,
                                                                       ref.fieldHash);
    if (!info)
    {
        if (outError) *outError = "component has no such field";
        return false;
    }

    out.field = reinterpret_cast<char*>(comp) + info->offset;
    out.info = info;
    return true;
}

bool ParsePropertyLiteral(const FieldRef& info, const char* text, double& out, double& out2)
{
    const auto type = static_cast<PropertyType>(info.type);
    if (type == PropertyType::String)
        return true;  // written verbatim, nothing to parse

    if (!text)
        return false;

    if (type == PropertyType::Bool)
    {
        out = (std::strcmp(text, "true") == 0 || std::strcmp(text, "1") == 0) ? 1.0 : 0.0;
        return true;
    }

    // Enums are authored as their index (the editor shows the value names and
    // stores the number), so every remaining type is plain numeric text.
    char* end = nullptr;
    const double v = std::strtod(text, &end);
    if (end == text)
        return false;
    out = v;

    if (type == PropertyType::Vector2)
    {
        // "x, y" — a Vector2 target needs both axes, so a single number is a
        // malformed literal rather than something to guess at.
        while (*end == ' ' || *end == ',')
            ++end;
        const char* second = end;
        const double v2 = std::strtod(second, &end);
        if (end == second)
            return false;
        out2 = v2;
    }
    return true;
}

bool ParsePropertyLiteral(const FieldRef& info, const char* text, double& out)
{
    double ignored = 0.0;
    return ParsePropertyLiteral(info, text, out, ignored);
}

namespace
{
// Read the field's live value as a double, for comparison. Types that are
// not orderable numbers (String is handled by the caller) report 0.
double ReadNumeric(const PropertyBinding& b)
{
    if (b.IsTransform())
    {
        Object* o = b.object;
        switch (b.transform)
        {
            case TransformField::X:
            case TransformField::Position: return o->GetX();
            case TransformField::Y: return o->GetY();
            case TransformField::Rotation: return o->GetLocalRotation();
            case TransformField::ScaleX:
            case TransformField::Scale: return o->GetScaleX();
            case TransformField::ScaleY: return o->GetScaleY();
            case TransformField::Active: return o->IsActive() ? 1.0 : 0.0;
            default: return 0.0;
        }
    }

    const void* f = b.field;
    switch (static_cast<PropertyType>(b.info->type))
    {
        case PropertyType::Vector2:
        {
            float v;
            std::memcpy(&v, f, sizeof v);
            return v;
        }
        case PropertyType::Float:
        {
            float v;
            std::memcpy(&v, f, sizeof v);
            return v;
        }
        case PropertyType::Double:
        {
            double v;
            std::memcpy(&v, f, sizeof v);
            return v;
        }
        case PropertyType::Bool:
        {
            bool v;
            std::memcpy(&v, f, sizeof v);
            return v ? 1.0 : 0.0;
        }
        case PropertyType::Int8:
        {
            int8_t v;
            std::memcpy(&v, f, sizeof v);
            return v;
        }
        case PropertyType::Int16:
        {
            int16_t v;
            std::memcpy(&v, f, sizeof v);
            return v;
        }
        case PropertyType::Int32:
        {
            int32_t v;
            std::memcpy(&v, f, sizeof v);
            return v;
        }
        case PropertyType::Int64:
        {
            int64_t v;
            std::memcpy(&v, f, sizeof v);
            return static_cast<double>(v);
        }
        case PropertyType::UInt8:
        {
            uint8_t v;
            std::memcpy(&v, f, sizeof v);
            return v;
        }
        case PropertyType::UInt16:
        {
            uint16_t v;
            std::memcpy(&v, f, sizeof v);
            return v;
        }
        case PropertyType::UInt32:
        {
            uint32_t v;
            std::memcpy(&v, f, sizeof v);
            return v;
        }
        case PropertyType::UInt64:
        {
            uint64_t v;
            std::memcpy(&v, f, sizeof v);
            return static_cast<double>(v);
        }
        case PropertyType::Enum:
            switch (b.info->enumSize)
            {
                case 1:
                {
                    uint8_t v;
                    std::memcpy(&v, f, 1);
                    return v;
                }
                case 2:
                {
                    uint16_t v;
                    std::memcpy(&v, f, 2);
                    return v;
                }
                default:
                {
                    uint32_t v;
                    std::memcpy(&v, f, 4);
                    return v;
                }
            }
        default:
            return 0.0;
    }
}
}  // namespace

double ReadBoundProperty(const PropertyBinding& b)
{
    if (!b.IsBound())
        return 0.0;
    return ReadNumeric(b);
}

double ReadBoundProperty2(const PropertyBinding& b)
{
    if (!b.IsBound() || static_cast<PropertyType>(b.info->type) != PropertyType::Vector2)
        return 0.0;

    if (b.IsTransform())
    {
        if (b.transform == TransformField::Position) return b.object->GetY();
        if (b.transform == TransformField::Scale) return b.object->GetScaleY();
        return 0.0;
    }
    float v;
    std::memcpy(&v, static_cast<const char*>(b.field) + sizeof(float), sizeof v);
    return v;
}

void WriteBoundNumbers(const PropertyBinding& b, double v, double v2)
{
    if (!b.IsBound())
        return;

    // Transform writes go through the setters so the world transform of this
    // object and every descendant is invalidated.
    if (b.IsTransform())
    {
        Object* o = b.object;
        switch (b.transform)
        {
            case TransformField::X: o->SetX(static_cast<float>(v)); break;
            case TransformField::Y: o->SetY(static_cast<float>(v)); break;
            case TransformField::Position:
                o->SetLocalPosition(static_cast<float>(v),
                                    static_cast<float>(v2));
                break;
            case TransformField::Rotation: o->SetRotation(static_cast<float>(v)); break;
            case TransformField::ScaleX: o->SetScaleX(static_cast<float>(v)); break;
            case TransformField::ScaleY: o->SetScaleY(static_cast<float>(v)); break;
            case TransformField::Scale:
                o->SetScale(static_cast<float>(v),
                            static_cast<float>(v2));
                break;
            case TransformField::Active: o->SetActive(v != 0.0); break;
            default: break;
        }
        return;
    }

    if (static_cast<PropertyType>(b.info->type) == PropertyType::Vector2)
    {
        const float xy[2] = { static_cast<float>(v), static_cast<float>(v2) };
        std::memcpy(b.field, xy, sizeof xy);
        return;
    }

    PropertyBinding tmp = b;
    tmp.number = v;
    WriteBoundProperty(tmp, std::string());
}

void WriteBoundProperty(const PropertyBinding& b, const std::string& text)
{
    if (!b.IsBound())
        return;

    // Transform and Vector2 targets are dispatched by WriteBoundNumbers.
    if (b.IsTransform() ||
        static_cast<PropertyType>(b.info->type) == PropertyType::Vector2)
    {
        WriteBoundNumbers(b, b.number, b.number2);
        return;
    }

    void* f = b.field;
    const double n = b.number;
    switch (static_cast<PropertyType>(b.info->type))
    {
        case PropertyType::String: *static_cast<std::string*>(f) = text; break;
        case PropertyType::Float:
        {
            float v = static_cast<float>(n);
            std::memcpy(f, &v, sizeof v);
            break;
        }
        case PropertyType::Double:
        {
            double v = n;
            std::memcpy(f, &v, sizeof v);
            break;
        }
        case PropertyType::Bool:
        {
            bool v = n != 0.0;
            std::memcpy(f, &v, sizeof v);
            break;
        }
        case PropertyType::Int8:
        {
            int8_t v = static_cast<int8_t>(n);
            std::memcpy(f, &v, sizeof v);
            break;
        }
        case PropertyType::Int16:
        {
            int16_t v = static_cast<int16_t>(n);
            std::memcpy(f, &v, sizeof v);
            break;
        }
        case PropertyType::Int32:
        {
            int32_t v = static_cast<int32_t>(n);
            std::memcpy(f, &v, sizeof v);
            break;
        }
        case PropertyType::Int64:
        {
            int64_t v = static_cast<int64_t>(n);
            std::memcpy(f, &v, sizeof v);
            break;
        }
        case PropertyType::UInt8:
        {
            uint8_t v = static_cast<uint8_t>(n);
            std::memcpy(f, &v, sizeof v);
            break;
        }
        case PropertyType::UInt16:
        {
            uint16_t v = static_cast<uint16_t>(n);
            std::memcpy(f, &v, sizeof v);
            break;
        }
        case PropertyType::UInt32:
        {
            uint32_t v = static_cast<uint32_t>(n);
            std::memcpy(f, &v, sizeof v);
            break;
        }
        case PropertyType::UInt64:
        {
            uint64_t v = static_cast<uint64_t>(n);
            std::memcpy(f, &v, sizeof v);
            break;
        }
        case PropertyType::Enum:
            switch (b.info->enumSize)
            {
                case 1:
                {
                    uint8_t v = static_cast<uint8_t>(n);
                    std::memcpy(f, &v, 1);
                    break;
                }
                case 2:
                {
                    uint16_t v = static_cast<uint16_t>(n);
                    std::memcpy(f, &v, 2);
                    break;
                }
                default:
                {
                    uint32_t v = static_cast<uint32_t>(n);
                    std::memcpy(f, &v, 4);
                    break;
                }
            }
            break;
        default:
            break;  // unsupported types are rejected at bind time by the caller
    }
}

int CompareBoundProperty(const PropertyBinding& b, const std::string& text)
{
    if (!b.IsBound())
        return 0;

    if (static_cast<PropertyType>(b.info->type) == PropertyType::String)
        return static_cast<const std::string*>(b.field)->compare(text);

    const double live = ReadNumeric(b);
    if (live < b.number) return -1;
    if (live > b.number) return 1;
    return 0;
}

}  // namespace Deki
