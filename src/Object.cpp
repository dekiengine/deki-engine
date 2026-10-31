#include <deki/Object.h>
#include <deki/Engine.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <type_traits>
#ifdef DEKI_EDITOR
#include <unordered_map>
#endif

#ifndef DEKI_EDITOR
#include <deki/Scene.h>
#else
#include <deki/reflection/ComponentRegistry.h>
#include "ObjectInternal.h"
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =============================================================================
// Layout tripwires
// =============================================================================
//
// Package DLLs reach `parent`, `children` and the component vector through
// inline accessors in Object.h, so they bake these offsets into their own
// code. Two things must therefore hold, and neither is checked by the linker:
//
//   1. The layout must not depend on DEKI_EDITOR. Build the editor and a
//      runtime config (see the engine CMakeLists) and confirm these asserts
//      report the SAME numbers in both. That is why the instance payload lives
//      in a side table rather than in members.
//   2. The layout must not change without the package ABI version changing.
//      If one of these fires, that is not a licence to update the number - it
//      means every prebuilt package is now wrong. Bump
//      kDekiReflectionAbiVersion in interop/ReflectionAbi.h in the same
//      commit, so the loader rejects stale DLLs instead of miscomputing.

namespace Deki
{

static_assert(!std::is_polymorphic<Object>::value,
              "Deki::Object gained a virtual function. That inserts a vptr, moves every "
              "member, and silently breaks every package DLL compiled against the old "
              "layout. If this is intended, bump kDekiReflectionAbiVersion.");

// Pinned on 64-bit desktop, where the editor and every package DLL are built.
// The editor and runtime builds of one transform width must agree: a
// difference sits underneath inline GetParent()/GetChildren() and makes every
// package read the wrong offsets.
//
// 32-bit targets (ESP32) are deliberately not asserted: they have their
// own consistent size, they load no DLLs, and hardcoding a second number here
// would just be a second thing to get wrong.
#if defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ == 8
// One number per transform width: the build chooses the width, and the ABI
// handshake carries it, so a 2D DLL is refused by a headless engine on the
// width before it ever gets to compare sizes.
#if defined(DEKI_TRANSFORM_3D)
constexpr size_t kExpectedObjectSize = 184;
#elif defined(DEKI_TRANSFORM_2D)
constexpr size_t kExpectedObjectSize = 152;
#else
constexpr size_t kExpectedObjectSize = 112;
#endif
static_assert(sizeof(Object) == kExpectedObjectSize,
              "sizeof(Deki::Object) changed. Every package DLL bakes this layout into its "
              "inline GetParent()/GetChildren()/GetComponent<T>() calls. Do NOT simply "
              "update this number: bump kDekiReflectionAbiVersion in "
              "interop/ReflectionAbi.h in the same commit so the loader refuses "
              "DLLs built against the old layout. Then verify the editor and a runtime "
              "config (-DDEKI_EDITOR=OFF -DSIMULATOR=ON) still agree.");
#endif

uint32_t Object::s_NextId = 1;

#ifdef DEKI_EDITOR
namespace
{
// Editor-only instance payload, keyed by object.
//
// Deliberately NOT a member: a member would make sizeof(Object) depend
// on DEKI_EDITOR, which is exactly the hazard this table avoids.
// Deliberately NOT in a header either - this accessor is non-inline and
// non-template, so there is exactly one table in the process. Inlining it
// would give every package DLL its own copy, and an object written through
// one would read back empty through another.
//
// Cost to the runtime build: nothing. The whole block is preprocessed away,
// so EditorInstanceData need not even be a complete type there.
struct EditorInstanceData
{
    std::string sourceSceneGuid;        // GUID of source scene
    std::string instanceOverridesJson;  // Overrides JSON (instance root only)
};

std::unordered_map<const Object*, EditorInstanceData>& EditorInstanceTable()
{
    static std::unordered_map<const Object*, EditorInstanceData> s_Table;
    return s_Table;
}

const std::string& EmptyString()
{
    static const std::string kEmpty;
    return kEmpty;
}
}  // namespace

const std::string& Object::GetSourceSceneGuid() const
{
    auto& table = EditorInstanceTable();
    auto it = table.find(this);
    return (it != table.end()) ? it->second.sourceSceneGuid : EmptyString();
}

void Object::SetSourceSceneGuid(const std::string& guid)
{
    // The flag mirrors "guid is non-empty"; IsSceneInstance() reads the flag.
    if (guid.empty())
    {
        m_EditorFlags &= static_cast<uint8_t>(~kEditorFlagSceneInstance);
        auto& table = EditorInstanceTable();
        auto it = table.find(this);
        if (it != table.end())
        {
            it->second.sourceSceneGuid.clear();
            // Drop the row entirely once nothing is left in it.
            if (it->second.instanceOverridesJson.empty())
            {
                table.erase(it);
            }
        }
        return;
    }

    m_EditorFlags |= kEditorFlagSceneInstance;
    EditorInstanceTable()[this].sourceSceneGuid = guid;
}

const std::string& Object::GetInstanceOverridesJson() const
{
    auto& table = EditorInstanceTable();
    auto it = table.find(this);
    return (it != table.end()) ? it->second.instanceOverridesJson : EmptyString();
}

void Object::SetInstanceOverridesJson(const std::string& json)
{
    if (json.empty())
    {
        auto& table = EditorInstanceTable();
        auto it = table.find(this);
        if (it != table.end())
        {
            it->second.instanceOverridesJson.clear();
            if (it->second.sourceSceneGuid.empty())
            {
                table.erase(it);
            }
        }
        return;
    }

    EditorInstanceTable()[this].instanceOverridesJson = json;
}
#endif  // DEKI_EDITOR

void Object::SetId(uint32_t newId)
{
    m_Id = newId;
    if (newId >= s_NextId)
    {
        s_NextId = newId + 1;
    }
}

Object::Object()
    : m_Id(s_NextId++),
      m_Name("DekiObject"),
      m_Active(true),
      m_OwnerScene(nullptr),
      m_Parent(nullptr)
{
}

Object::Object(const std::string& objectName)
    : m_Id(s_NextId++),
      m_Name(objectName),
      m_Active(true),
      m_OwnerScene(nullptr),
      m_Parent(nullptr)
{
}

Object::~Object()
{
#ifdef DEKI_EDITOR
    // Drop this object's row from the editor-only instance side table. Keyed by
    // `this`, so leaving it behind would both leak and hand stale data to the
    // next object that happened to be allocated at this address.
    EditorInstanceTable().erase(this);
#endif

    if (m_Parent)
    {
        m_Parent->RemoveChild(this);
    }

    // Delete all children
    for (auto* child : m_Children)
    {
        child->m_Parent = nullptr;  // Prevent child from trying to remove itself from this object
        delete child;
    }
    m_Children.clear();

    // Delete all components
    for (auto* comp : m_Components)
    {
#ifdef DEKI_EDITOR
        Internal::ForgetComponentData(comp);
#endif
        delete comp;
    }
    m_Components.clear();
}

// Hierarchy management implementations
void Object::AddChild(Object* child)
{
    if (!child || child == this || child->m_Parent == this)
    {
        return;
    }

    // Remove from previous parent
    if (child->m_Parent)
    {
        child->m_Parent->RemoveChild(child);
    }

    m_Children.push_back(child);
    child->m_Parent = this;

    // Propagate scene to child (always update if different, handles stale/dangling pointers)
    if (m_OwnerScene && child->m_OwnerScene != m_OwnerScene)
    {
        child->SetOwnerScene(m_OwnerScene);
    }

    // Mark child's world transform dirty (parent changed)
    child->MarkWorldTransformDirty();
}

void Object::RemoveChild(Object* child)
{
    if (!child)
    {
        return;
    }

    auto it = std::find(m_Children.begin(), m_Children.end(), child);
    if (it != m_Children.end())
    {
        (*it)->m_Parent = nullptr;
        (*it)->MarkWorldTransformDirty();  // Parent changed, world transform needs recalc
        m_Children.erase(it);
    }
}

void Object::SetParent(Object* newParent)
{
    if (newParent == m_Parent)
    {
        return;
    }

    // Prevent setting self or descendant as parent
    if (newParent && (newParent == this || newParent->IsDescendantOf(this)))
    {
        return;
    }

    if (newParent)
    {
        newParent->AddChild(this);
    }
    else
    {
        RemoveFromParent();
    }
}

void Object::RemoveFromParent()
{
    if (m_Parent)
    {
        m_Parent->RemoveChild(this);
    }
}

void Object::InsertChildAt(Object* child, size_t index)
{
    if (!child || child == this || child->m_Parent == this)
    {
        return;
    }

    // Remove from previous parent
    if (child->m_Parent)
    {
        child->m_Parent->RemoveChild(child);
    }

    // Clamp index to valid range
    if (index > m_Children.size())
    {
        index = m_Children.size();
    }

    m_Children.insert(m_Children.begin() + index, child);
    child->m_Parent = this;

    // Propagate scene to child (always update if different, handles stale/dangling pointers)
    if (m_OwnerScene && child->m_OwnerScene != m_OwnerScene)
    {
        child->SetOwnerScene(m_OwnerScene);
    }

    // Mark child's world transform dirty (parent changed)
    child->MarkWorldTransformDirty();
}

int Object::GetChildIndex(Object* child) const
{
    auto it = std::find(m_Children.begin(), m_Children.end(), child);
    if (it != m_Children.end())
    {
        return static_cast<int>(std::distance(m_Children.begin(), it));
    }
    return -1;
}

Object* Object::FindChild(uint32_t childId)
{
    for (auto* child : m_Children)
    {
        if (child->GetId() == childId)
        {
            return child;
        }

        // Recursive search
        Object* found = child->FindChild(childId);
        if (found)
        {
            return found;
        }
    }
    return nullptr;
}

Object* Object::FindChildByName(const std::string& searchName)
{
    for (auto* child : m_Children)
    {
        if (child->GetName() == searchName)
        {
            return child;
        }

        // Recursive search
        Object* found = child->FindChildByName(searchName);
        if (found)
        {
            return found;
        }
    }
    return nullptr;
}

bool Object::IsDescendantOf(Object* ancestor) const
{
    if (!ancestor)
    {
        return false;
    }

    Object* current = m_Parent;
    while (current)
    {
        if (current == ancestor)
        {
            return true;
        }
        current = current->m_Parent;
    }
    return false;
}

void Object::SetActive(bool value)
{
    if (m_Active == value)
    {
        return;
    }
    m_Active = value;
    if (m_OwnerScene)
    {
        m_OwnerScene->InvalidateComponentsCache();
    }
}

bool Object::IsActiveInHierarchy() const
{
    // Walk up without recursing: one frame, no call per ancestor.
    for (const Object* o = this; o; o = o->m_Parent)
    {
        if (!o->m_Active)
        {
            return false;
        }
    }
    return true;
}

#ifdef DEKI_TRANSFORM_2D

#ifdef DEKI_TRANSFORM_3D
namespace
{
// The engine's angle convention: rotate about X, then Y, then Z, radians,
// right-handed. R = Rz * Ry * Rx, row-major, m[row * 3 + col].
void RotationFromAngles(float rx, float ry, float rz, float m[9])
{
    const float cx = std::cos(rx), sx = std::sin(rx);
    const float cy = std::cos(ry), sy = std::sin(ry);
    const float cz = std::cos(rz), sz = std::sin(rz);
    m[0] = cz * cy;
    m[1] = cz * sy * sx - sz * cx;
    m[2] = cz * sy * cx + sz * sx;
    m[3] = sz * cy;
    m[4] = sz * sy * sx + cz * cx;
    m[5] = sz * sy * cx - cz * sx;
    m[6] = -sy;
    m[7] = cy * sx;
    m[8] = cy * cx;
}

// The angles back out of a matrix RotationFromAngles built.
void AnglesFromRotation(const float m[9], float& rx, float& ry, float& rz)
{
    const float sy = -m[6];
    if (sy > 0.9999999f || sy < -0.9999999f)
    {
        // Looking straight along Y: X and Z rotation are not separable, so
        // all of it is reported as Z.
        ry = sy > 0.0f ? 1.5707963f : -1.5707963f;
        rx = 0.0f;
        rz = std::atan2(-m[1], m[4]);
        return;
    }
    ry = std::asin(sy);
    rx = std::atan2(m[7], m[8]);
    rz = std::atan2(m[3], m[0]);
}

void Multiply(const float a[9], const float b[9], float out[9])
{
    for (int r = 0; r < 3; ++r)
    {
        for (int c = 0; c < 3; ++c)
        {
            out[r * 3 + c] = a[r * 3 + 0] * b[0 * 3 + c] + a[r * 3 + 1] * b[1 * 3 + c] + a[r * 3 + 2] * b[2 * 3 + c];
        }
    }
}

void Apply(const float m[9], float x, float y, float z, float& ox, float& oy, float& oz)
{
    ox = m[0] * x + m[1] * y + m[2] * z;
    oy = m[3] * x + m[4] * y + m[5] * z;
    oz = m[6] * x + m[7] * y + m[8] * z;
}

// The transpose, which is the inverse of a rotation.
void ApplyInverse(const float m[9], float x, float y, float z, float& ox, float& oy, float& oz)
{
    ox = m[0] * x + m[3] * y + m[6] * z;
    oy = m[1] * x + m[4] * y + m[7] * z;
    oz = m[2] * x + m[5] * y + m[8] * z;
}
}  // namespace
#endif  // DEKI_TRANSFORM_3D

void Object::RecomputeWorldTransform() const
{
    if (m_Parent)
    {
        // Ensure parent is valid first (recursive up the chain)
        m_Parent->EnsureWorldTransformValid();

#ifdef DEKI_TRANSFORM_3D
        // Only when something in the chain rotates about X or Y does the
        // composition need matrices. Everything else takes the path below,
        // which is the 2D engine's exactly, so 2D content is bit-identical.
        const bool needsMatrices = m_Parent->m_WorldRotationX != 0.0f || m_Parent->m_WorldRotationY != 0.0f ||
                                   m_RotationX != 0.0f || m_RotationY != 0.0f;
        if (needsMatrices)
        {
            m_WorldScaleX = m_ScaleX * m_Parent->m_WorldScaleX;
            m_WorldScaleY = m_ScaleY * m_Parent->m_WorldScaleY;
            m_WorldScaleZ = m_ScaleZ * m_Parent->m_WorldScaleZ;

            float rp[9], rl[9], rw[9];
            RotationFromAngles(m_Parent->m_WorldRotationX, m_Parent->m_WorldRotationY, m_Parent->m_WorldRotation, rp);
            RotationFromAngles(m_RotationX, m_RotationY, m_Rotation, rl);
            Multiply(rp, rl, rw);
            AnglesFromRotation(rw, m_WorldRotationX, m_WorldRotationY, m_WorldRotation);

            float ox, oy, oz;
            Apply(rp, m_X * m_Parent->m_WorldScaleX, m_Y * m_Parent->m_WorldScaleY, m_Z * m_Parent->m_WorldScaleZ, ox,
                  oy, oz);
            m_WorldX = m_Parent->m_WorldX + ox;
            m_WorldY = m_Parent->m_WorldY + oy;
            m_WorldZ = m_Parent->m_WorldZ + oz;

            m_WorldTransformDirty = false;
            return;
        }
        // Z-only rotation leaves the third axis alone.
        m_WorldZ = m_Parent->m_WorldZ + m_Z * m_Parent->m_WorldScaleZ;
        m_WorldScaleZ = m_ScaleZ * m_Parent->m_WorldScaleZ;
        m_WorldRotationX = 0.0f;
        m_WorldRotationY = 0.0f;
#endif

        // Calculate world transform from parent
        m_WorldScaleX = m_ScaleX * m_Parent->m_WorldScaleX;
        m_WorldScaleY = m_ScaleY * m_Parent->m_WorldScaleY;
        m_WorldRotation = m_Rotation + m_Parent->m_WorldRotation;

        const float scaledX = m_X * m_Parent->m_WorldScaleX;
        const float scaledY = m_Y * m_Parent->m_WorldScaleY;

        if (m_Parent->m_WorldRotation == 0.0f)
        {
            // The overwhelmingly common case: no cos/sin. Bit-identical to the
            // trig path (cos 0 = 1, sin 0 = 0 exactly, and a + (b * 0) == a).
            m_WorldX = m_Parent->m_WorldX + scaledX;
            m_WorldY = m_Parent->m_WorldY + scaledY;
        }
        else
        {
            // Apply parent rotation (radians) to local position.
            const float cosR = std::cos(m_Parent->m_WorldRotation);
            const float sinR = std::sin(m_Parent->m_WorldRotation);
            m_WorldX = m_Parent->m_WorldX + (scaledX * cosR - scaledY * sinR);
            m_WorldY = m_Parent->m_WorldY + (scaledX * sinR + scaledY * cosR);
        }
    }
    else
    {
        // No parent, world transform equals local
        m_WorldX = m_X;
        m_WorldY = m_Y;
        m_WorldRotation = m_Rotation;
        m_WorldScaleX = m_ScaleX;
        m_WorldScaleY = m_ScaleY;
#ifdef DEKI_TRANSFORM_3D
        m_WorldZ = m_Z;
        m_WorldRotationX = m_RotationX;
        m_WorldRotationY = m_RotationY;
        m_WorldScaleZ = m_ScaleZ;
#endif
    }

    m_WorldTransformDirty = false;
}

void Object::MarkWorldTransformDirty()
{
    if (m_WorldTransformDirty)
    {
        return;  // Already dirty, no need to propagate again
    }

    m_WorldTransformDirty = true;

    // Recursively mark all children dirty
    for (auto* child : m_Children)
    {
        child->MarkWorldTransformDirty();
    }
}

void Object::SetLocalPosition(float newX, float newY)
{
    m_X = newX;
    m_Y = newY;
    MarkWorldTransformDirty();
}

void Object::SetWorldPosition(float worldX, float worldY)
{
    if (m_Parent)
    {
#ifdef DEKI_TRANSFORM_3D
        // A parent turned about X or Y mixes all three axes, so the plane
        // arithmetic below does not apply; undo its rotation properly, keeping
        // this object's current depth.
        if (m_Parent->GetWorldRotationX() != 0.0f || m_Parent->GetWorldRotationY() != 0.0f)
        {
            float rp[9];
            RotationFromAngles(m_Parent->GetWorldRotationX(), m_Parent->GetWorldRotationY(),
                               m_Parent->GetWorldRotation(), rp);
            float lx, ly, lz;
            ApplyInverse(rp, worldX - m_Parent->GetWorldX(), worldY - m_Parent->GetWorldY(),
                         GetWorldZ() - m_Parent->GetWorldZ(), lx, ly, lz);
            m_X = lx / m_Parent->GetWorldScaleX();
            m_Y = ly / m_Parent->GetWorldScaleY();
            m_Z = lz / m_Parent->GetWorldScaleZ();
            MarkWorldTransformDirty();
            return;
        }
#endif
        // Calculate local position from world position
        // Use getters to ensure parent's world transform is valid
        float dx = worldX - m_Parent->GetWorldX();
        float dy = worldY - m_Parent->GetWorldY();

        // Inverse rotation: negate parent's rotation (radians).
        float negRot = -m_Parent->GetWorldRotation();
        float cosR = std::cos(negRot);
        float sinR = std::sin(negRot);

        // Unrotate
        float unrotatedX = dx * cosR - dy * sinR;
        float unrotatedY = dx * sinR + dy * cosR;

        // Unscale
        m_X = unrotatedX / m_Parent->GetWorldScaleX();
        m_Y = unrotatedY / m_Parent->GetWorldScaleY();
    }
    else
    {
        // No parent, local = world
        m_X = worldX;
        m_Y = worldY;
    }

    MarkWorldTransformDirty();
}

void Object::SetWorldPositionPx(float pxX, float pxY)
{
    // Convert pixel-space input to stored meters via the project's PPM.
    const auto& settings = EngineSettings::Global();
    const float ppm = settings.pixelsPerMeter > 0.0f ? settings.pixelsPerMeter : 1.0f;
    const float inv = 1.0f / ppm;
    SetWorldPosition(pxX * inv, pxY * inv);
}

void Object::SetScale(float scaleX, float scaleY)
{
    m_ScaleX = scaleX;
    m_ScaleY = scaleY;
    MarkWorldTransformDirty();
}

void Object::SetRotation(float newRotation)
{
    // Radians (engine convention).
    m_Rotation = newRotation;
    MarkWorldTransformDirty();
}

void Object::SetX(float newX)
{
    m_X = newX;
    MarkWorldTransformDirty();
}

void Object::SetY(float newY)
{
    m_Y = newY;
    MarkWorldTransformDirty();
}

void Object::SetScaleX(float newScaleX)
{
    m_ScaleX = newScaleX;
    MarkWorldTransformDirty();
}

void Object::SetScaleY(float newScaleY)
{
    m_ScaleY = newScaleY;
    MarkWorldTransformDirty();
}

#ifdef DEKI_TRANSFORM_3D
void Object::SetZ(float newZ)
{
    m_Z = newZ;
    MarkWorldTransformDirty();
}

void Object::SetRotationX(float radians)
{
    m_RotationX = radians;
    MarkWorldTransformDirty();
}

void Object::SetRotationY(float radians)
{
    m_RotationY = radians;
    MarkWorldTransformDirty();
}

void Object::SetScaleZ(float newScaleZ)
{
    m_ScaleZ = newScaleZ;
    MarkWorldTransformDirty();
}
#endif  // DEKI_TRANSFORM_3D

#endif  // DEKI_TRANSFORM_2D

void Internal::AttachComponent(Object& object, Component* component)
{
    object.AddComponent(component);
}

#ifdef DEKI_EDITOR
Component* Internal::AddComponentByName(Object& object, const std::string& typeName)
{
    return object.AddComponent(typeName);
}

int Internal::DetachComponent(Object& object, Component* component)
{
    auto& comps = object.m_Components;
    for (size_t i = 0; i < comps.size(); ++i)
    {
        if (comps[i] == component)
        {
            comps.erase(comps.begin() + static_cast<std::ptrdiff_t>(i));
            return static_cast<int>(i);
        }
    }
    return -1;
}

void Internal::ReattachComponent(Object& object, Component* component, size_t index)
{
    if (!component)
    {
        return;
    }
    component->SetOwner(&object);
    auto& comps = object.m_Components;
    if (index > comps.size())
    {
        index = comps.size();
    }
    comps.insert(comps.begin() + static_cast<std::ptrdiff_t>(index), component);
}

Component* Object::AddComponent(const std::string& typeName)
{
    auto& registry = ComponentRegistry::Instance();
    // The "type" key is the qualified class name; a bare class name is
    // accepted too.
    const ComponentMeta* meta = registry.GetMeta(typeName);
    if (!meta)
    {
        meta = registry.GetMetaByClassName(typeName);
    }
    if (!meta)
    {
        const char* sceneName = m_OwnerScene ? m_OwnerScene->GetName().c_str() : "<no scene>";
        DEKI_LOG(LogLevel::Warning,
                 "AddComponent: '%s' not registered on object '%s' (id=%u, scene='%s'). The package providing it "
                 "is likely not loaded.",
                 typeName.c_str(), m_Name.c_str(), m_Id, sceneName);
        return nullptr;
    }
    if (!meta->createFunc || meta->isAbstract)
    {
        return nullptr;
    }
    Component* comp = meta->createFunc();
    if (comp)
    {
        comp->SetOwner(this);
        m_Components.push_back(comp);
    }
    return comp;
}
#endif

}  // namespace Deki
