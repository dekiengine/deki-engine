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
//      report the SAME numbers in both. That is the whole point of keeping the
//      instance payload in a side table rather than in members.
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
// Measured on both configurations at the time the instance payload moved to the
// side table: editor and runtime agree at 152. Before that move they were 224
// and 152 respectively - a 72-byte divergence sitting underneath inline
// GetParent()/GetChildren(), which is the bug this pins shut.
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
// on DEKI_EDITOR, which is exactly the hazard this table exists to remove.
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
    static std::unordered_map<const Object*, EditorInstanceData> table;
    return table;
}

const std::string& EmptyString()
{
    static const std::string empty;
    return empty;
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
    // The flag mirrors "guid is non-empty", which is what IsSceneInstance()
    // used to compute directly from the string.
    if (guid.empty())
    {
        m_EditorFlags &= static_cast<uint8_t>(~EditorFlag_SceneInstance);
        auto& table = EditorInstanceTable();
        auto it = table.find(this);
        if (it != table.end())
        {
            it->second.sourceSceneGuid.clear();
            // Drop the row entirely once nothing is left in it.
            if (it->second.instanceOverridesJson.empty())
                table.erase(it);
        }
        return;
    }

    m_EditorFlags |= EditorFlag_SceneInstance;
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
                table.erase(it);
        }
        return;
    }

    EditorInstanceTable()[this].instanceOverridesJson = json;
}
#endif  // DEKI_EDITOR

void Object::SetId(uint32_t newId)
{
    id = newId;
    if (newId >= s_NextId) s_NextId = newId + 1;
}

Object::Object()
    : id(s_NextId++), name("DekiObject"), active(true), ownerScene(nullptr), parent(nullptr)
{
}

Object::Object(const std::string& objectName)
    : id(s_NextId++), name(objectName), active(true), ownerScene(nullptr), parent(nullptr)
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

    // Remove from parent if exists
    if (parent)
    {
        parent->RemoveChild(this);
    }

    // Delete all children
    for (auto* child : children)
    {
        child->parent = nullptr;  // Prevent child from trying to remove itself from this object
        delete child;
    }
    children.clear();

    // Delete all components
    for (auto* comp : components)
    {
#ifdef DEKI_EDITOR
        Internal::ForgetComponentData(comp);
#endif
        delete comp;
    }
    components.clear();
}

// Hierarchy management implementations
void Object::AddChild(Object* child)
{
    if (!child || child == this || child->parent == this)
        return;

    // Remove from previous parent
    if (child->parent)
    {
        child->parent->RemoveChild(child);
    }

    // Add to our children
    children.push_back(child);
    child->parent = this;

    // Propagate scene to child (always update if different, handles stale/dangling pointers)
    if (ownerScene && child->ownerScene != ownerScene)
    {
        child->SetOwnerScene(ownerScene);
    }

    // Mark child's world transform dirty (parent changed)
    child->MarkWorldTransformDirty();
}

void Object::RemoveChild(Object* child)
{
    if (!child)
        return;

    auto it = std::find(children.begin(), children.end(), child);
    if (it != children.end())
    {
        (*it)->parent = nullptr;
        (*it)->MarkWorldTransformDirty();  // Parent changed, world transform needs recalc
        children.erase(it);
    }
}

void Object::SetParent(Object* newParent)
{
    if (newParent == parent)
        return;

    // Prevent setting self or descendant as parent
    if (newParent && (newParent == this || newParent->IsDescendantOf(this)))
        return;

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
    if (parent)
    {
        parent->RemoveChild(this);
    }
}

void Object::InsertChildAt(Object* child, size_t index)
{
    if (!child || child == this || child->parent == this)
        return;

    // Remove from previous parent
    if (child->parent)
    {
        child->parent->RemoveChild(child);
    }

    // Clamp index to valid range
    if (index > children.size())
        index = children.size();

    // Insert at position
    children.insert(children.begin() + index, child);
    child->parent = this;

    // Propagate scene to child (always update if different, handles stale/dangling pointers)
    if (ownerScene && child->ownerScene != ownerScene)
    {
        child->SetOwnerScene(ownerScene);
    }

    // Mark child's world transform dirty (parent changed)
    child->MarkWorldTransformDirty();
}

int Object::GetChildIndex(Object* child) const
{
    auto it = std::find(children.begin(), children.end(), child);
    if (it != children.end())
        return static_cast<int>(std::distance(children.begin(), it));
    return -1;
}

Object* Object::FindChild(uint32_t childId)
{
    for (auto* child : children)
    {
        if (child->GetId() == childId)
            return child;

        // Recursive search
        Object* found = child->FindChild(childId);
        if (found)
            return found;
    }
    return nullptr;
}

Object* Object::FindChildByName(const std::string& searchName)
{
    for (auto* child : children)
    {
        if (child->GetName() == searchName)
            return child;

        // Recursive search
        Object* found = child->FindChildByName(searchName);
        if (found)
            return found;
    }
    return nullptr;
}

bool Object::IsDescendantOf(Object* ancestor) const
{
    if (!ancestor)
        return false;

    Object* current = parent;
    while (current)
    {
        if (current == ancestor)
            return true;
        current = current->parent;
    }
    return false;
}

bool Object::IsActiveInHierarchy() const
{
    // Walk up without recursing: one frame, no call per ancestor.
    for (const Object* o = this; o; o = o->parent)
    {
        if (!o->active)
            return false;
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
    m[0] = cz * cy;  m[1] = cz * sy * sx - sz * cx;  m[2] = cz * sy * cx + sz * sx;
    m[3] = sz * cy;  m[4] = sz * sy * sx + cz * cx;  m[5] = sz * sy * cx - cz * sx;
    m[6] = -sy;      m[7] = cy * sx;                 m[8] = cy * cx;
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
        for (int c = 0; c < 3; ++c)
            out[r * 3 + c] = a[r * 3 + 0] * b[0 * 3 + c] + a[r * 3 + 1] * b[1 * 3 + c] + a[r * 3 + 2] * b[2 * 3 + c];
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
    if (parent)
    {
        // Ensure parent is valid first (recursive up the chain)
        parent->EnsureWorldTransformValid();

#ifdef DEKI_TRANSFORM_3D
        // Only when something in the chain rotates about X or Y does the
        // composition need matrices. Everything else takes the path below,
        // which is the 2D engine's exactly, so 2D content is bit-identical.
        const bool needsMatrices = parent->m_WorldRotationX != 0.0f || parent->m_WorldRotationY != 0.0f ||
                                   rotationX != 0.0f || rotationY != 0.0f;
        if (needsMatrices)
        {
            m_WorldScaleX = m_ScaleX * parent->m_WorldScaleX;
            m_WorldScaleY = m_ScaleY * parent->m_WorldScaleY;
            m_WorldScaleZ = m_ScaleZ * parent->m_WorldScaleZ;

            float rp[9], rl[9], rw[9];
            RotationFromAngles(parent->m_WorldRotationX, parent->m_WorldRotationY, parent->m_WorldRotation, rp);
            RotationFromAngles(rotationX, rotationY, rotation, rl);
            Multiply(rp, rl, rw);
            AnglesFromRotation(rw, m_WorldRotationX, m_WorldRotationY, m_WorldRotation);

            float ox, oy, oz;
            Apply(rp, x * parent->m_WorldScaleX, y * parent->m_WorldScaleY, z * parent->m_WorldScaleZ, ox, oy, oz);
            m_WorldX = parent->m_WorldX + ox;
            m_WorldY = parent->m_WorldY + oy;
            m_WorldZ = parent->m_WorldZ + oz;

            m_WorldTransformDirty = false;
            return;
        }
        // Z-only rotation leaves the third axis alone.
        m_WorldZ = parent->m_WorldZ + z * parent->m_WorldScaleZ;
        m_WorldScaleZ = m_ScaleZ * parent->m_WorldScaleZ;
        m_WorldRotationX = 0.0f;
        m_WorldRotationY = 0.0f;
#endif

        // Calculate world transform from parent
        m_WorldScaleX = m_ScaleX * parent->m_WorldScaleX;
        m_WorldScaleY = m_ScaleY * parent->m_WorldScaleY;
        m_WorldRotation = rotation + parent->m_WorldRotation;

        const float scaled_x = x * parent->m_WorldScaleX;
        const float scaled_y = y * parent->m_WorldScaleY;

        if (parent->m_WorldRotation == 0.0f)
        {
            // The overwhelmingly common case: no cos/sin. Bit-identical to the
            // trig path (cos 0 = 1, sin 0 = 0 exactly, and a + (b * 0) == a).
            m_WorldX = parent->m_WorldX + scaled_x;
            m_WorldY = parent->m_WorldY + scaled_y;
        }
        else
        {
            // Apply parent rotation (radians) to local position.
            const float cos_r = std::cos(parent->m_WorldRotation);
            const float sin_r = std::sin(parent->m_WorldRotation);
            m_WorldX = parent->m_WorldX + (scaled_x * cos_r - scaled_y * sin_r);
            m_WorldY = parent->m_WorldY + (scaled_x * sin_r + scaled_y * cos_r);
        }
    }
    else
    {
        // No parent, world transform equals local
        m_WorldX = x;
        m_WorldY = y;
        m_WorldRotation = rotation;
        m_WorldScaleX = m_ScaleX;
        m_WorldScaleY = m_ScaleY;
#ifdef DEKI_TRANSFORM_3D
        m_WorldZ = z;
        m_WorldRotationX = rotationX;
        m_WorldRotationY = rotationY;
        m_WorldScaleZ = m_ScaleZ;
#endif
    }

    m_WorldTransformDirty = false;
}

void Object::MarkWorldTransformDirty()
{
    if (m_WorldTransformDirty)
        return;  // Already dirty, no need to propagate again

    m_WorldTransformDirty = true;

    // Recursively mark all children dirty
    for (auto* child : children)
    {
        child->MarkWorldTransformDirty();
    }
}

void Object::SetLocalPosition(float newX, float newY)
{
    x = newX;
    y = newY;
    MarkWorldTransformDirty();
}

void Object::SetWorldPosition(float worldX, float worldY)
{
    if (parent)
    {
#ifdef DEKI_TRANSFORM_3D
        // A parent turned about X or Y mixes all three axes, so the plane
        // arithmetic below does not apply; undo its rotation properly, keeping
        // this object's current depth.
        if (parent->GetWorldRotationX() != 0.0f || parent->GetWorldRotationY() != 0.0f)
        {
            float rp[9];
            RotationFromAngles(parent->GetWorldRotationX(), parent->GetWorldRotationY(),
                               parent->GetWorldRotation(), rp);
            float lx, ly, lz;
            ApplyInverse(rp, worldX - parent->GetWorldX(), worldY - parent->GetWorldY(),
                         GetWorldZ() - parent->GetWorldZ(), lx, ly, lz);
            x = lx / parent->GetWorldScaleX();
            y = ly / parent->GetWorldScaleY();
            z = lz / parent->GetWorldScaleZ();
            MarkWorldTransformDirty();
            return;
        }
#endif
        // Calculate local position from world position
        // Use getters to ensure parent's world transform is valid
        float dx = worldX - parent->GetWorldX();
        float dy = worldY - parent->GetWorldY();

        // Inverse rotation: negate parent's rotation (radians).
        float negRot = -parent->GetWorldRotation();
        float cos_r = std::cos(negRot);
        float sin_r = std::sin(negRot);

        // Unrotate
        float unrotated_x = dx * cos_r - dy * sin_r;
        float unrotated_y = dx * sin_r + dy * cos_r;

        // Unscale
        x = unrotated_x / parent->GetWorldScaleX();
        y = unrotated_y / parent->GetWorldScaleY();
    }
    else
    {
        // No parent, local = world
        x = worldX;
        y = worldY;
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
    rotation = newRotation;
    MarkWorldTransformDirty();
}

void Object::SetX(float newX)
{
    x = newX;
    MarkWorldTransformDirty();
}

void Object::SetY(float newY)
{
    y = newY;
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
    z = newZ;
    MarkWorldTransformDirty();
}

void Object::SetRotationX(float radians)
{
    rotationX = radians;
    MarkWorldTransformDirty();
}

void Object::SetRotationY(float radians)
{
    rotationY = radians;
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
Component* Internal::AddComponentByName(Object& object, const std::string& typeName, const std::string& classHint)
{
    return object.AddComponent(typeName, classHint);
}

int Internal::DetachComponent(Object& object, Component* component)
{
    auto& comps = object.components;
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
        return;
    component->SetOwner(&object);
    auto& comps = object.components;
    if (index > comps.size())
        index = comps.size();
    comps.insert(comps.begin() + static_cast<std::ptrdiff_t>(index), component);
}

Component* Object::AddComponent(const std::string& typeName, const std::string& classHint)
{
    auto& registry = ComponentRegistry::Instance();
    // The "type" key is the qualified class name; a bare class name and a
    // 0.10 GUID (through the legacy table) are accepted too.
    const ComponentMeta* meta = registry.GetMeta(typeName);
    if (!meta)
        meta = registry.GetMetaByClassName(typeName);
    if (!meta && !classHint.empty())
    {
        // The type in the file is not registered but the class name written
        // beside it is: the class was renamed or moved, or the file predates
        // 0.11 and holds a project component's GUID. Load it by name; the next
        // save writes the current type.
        meta = registry.GetMetaByClassName(classHint);
        if (meta)
            DEKI_LOG(LogLevel::Warning,
                     "AddComponent: type '%s' is not registered; resolved '%s' by class name "
                     "(object='%s' id=%u). Re-save the scene to write the current type.",
                     typeName.c_str(), classHint.c_str(), name.c_str(), id);
    }
    if (!meta)
    {
        const bool looksLikeGuid = typeName.size() == 36 && typeName[8] == '-';
        const char* sceneName = ownerScene ? ownerScene->GetName().c_str() : "<no scene>";
        if (!classHint.empty())
        {
            DEKI_LOG(LogLevel::Warning,
                     "AddComponent: '%s' (type %s) not registered on object '%s' (id=%u, scene='%s'). "
                     "Package providing this component is likely not loaded.",
                     classHint.c_str(), typeName.c_str(), name.c_str(), id, sceneName);
        }
        else if (looksLikeGuid)
        {
            DEKI_LOG(LogLevel::Warning,
                     "AddComponent: 0.10 GUID '%s' not registered - class name unknown "
                     "(object='%s' id=%u scene='%s'). Re-save this scene in the editor to embed a class-name hint.",
                     typeName.c_str(), name.c_str(), id, sceneName);
        }
        else
        {
            DEKI_LOG(LogLevel::Warning,
                     "AddComponent: class '%s' not registered "
                     "(object='%s' id=%u scene='%s'). Check spelling or that its package is loaded.",
                     typeName.c_str(), name.c_str(), id, sceneName);
        }
        return nullptr;
    }
    if (!meta->createFunc || meta->isAbstract)
        return nullptr;
    Component* comp = meta->createFunc();
    if (comp)
    {
        comp->SetOwner(this);
        components.push_back(comp);
    }
    return comp;
}
#endif

}  // namespace Deki
