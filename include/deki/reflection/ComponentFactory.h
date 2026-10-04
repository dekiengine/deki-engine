#pragma once

// The table of every registered component class, keyed by type id.
//
// Engine-internal. Scripts and packages use `obj->AddComponent<Health>()` and
// `obj->GetComponent<Health>()` instead. Only the editor and the scene loaders
// create components from a name or an id.
//
// At startup the generated registration of the engine core, each package and
// the project hands its ComponentTypeInfo entries here. The scene loader then
// creates components by id, and AddComponent<T> looks up the entry to attach
// to the instance it constructs.

#include <deki/Component.h>
#include <type_traits>
#include <unordered_map>

// DLL export/import in editor builds, so all DLLs share one ComponentFactory.
#ifdef DEKI_EDITOR
#ifdef _WIN32
#ifdef DEKI_ENGINE_EXPORTS
#define COMPONENT_FACTORY_API __declspec(dllexport)
#else
#define COMPONENT_FACTORY_API __declspec(dllimport)
#endif
#else
#define COMPONENT_FACTORY_API
#endif
#else
#define COMPONENT_FACTORY_API
#endif

namespace Deki
{

/// Creates components by type id and tells which class an id belongs to.
/// Lookups are O(1).
class COMPONENT_FACTORY_API ComponentFactory
{
public:
    static ComponentFactory& Instance();

    /// Registers a component class. Registering the same id again replaces the
    /// entry, which a hot-reloaded package needs.
    void Register(const ComponentTypeInfo* info)
    {
        if (info)
        {
            m_Types[info->typeId] = info;
        }
    }

    /// Removes a component class, when its package unloads.
    void Unregister(uint32_t typeId) { m_Types.erase(typeId); }

    /// The class entry for a type id, or nullptr when unknown.
    const ComponentTypeInfo* Find(uint32_t typeId) const
    {
        auto it = m_Types.find(typeId);
        return it != m_Types.end() ? it->second : nullptr;
    }

    /// Creates a component by type id, with its type info attached. Returns
    /// nullptr when the id is unknown or the class is abstract.
    Component* Create(uint32_t typeId) const
    {
        const ComponentTypeInfo* info = Find(typeId);
        return (info && info->create) ? info->create() : nullptr;
    }

    bool IsRegistered(uint32_t typeId) const { return m_Types.find(typeId) != m_Types.end(); }

    size_t GetRegisteredCount() const { return m_Types.size(); }

    /// Removes every registered class, for plugin hot-reload.
    void Clear() { m_Types.clear(); }

private:
    ComponentFactory() = default;
    std::unordered_map<uint32_t, const ComponentTypeInfo*> m_Types;
};

/// A class entry for a component the codegen never saw (a test double, a type
/// defined in a .cpp). It identifies the type and says whether Update() runs.
/// It has no deserializer, so scenes can load it only when `meta` supplies its
/// own createFunc.
namespace Detail
{
template <class T, class Base>
constexpr ComponentType HandWrittenBaseId()
{
    if constexpr (std::is_void<Base>::value)
    {
        return TypeId<Component>();
    }
    else
    {
        return TypeId<Base>();
    }
}
// True when T, or a base between T and Component, declares Update(). The
// member pointer's class is the declaring class, so an inherited
// Component::Update gives Component's pointer type.
template <class T>
constexpr bool DeclaresUpdate()
{
    return !std::is_same<decltype(&T::Update), void (Component::*)()>::value;
}
}  // namespace Detail

/// `Base` names the direct component base when it is not Deki::Component (a
/// test renderer deriving from RendererComponent), so lookups by base type
/// find the instance. A class that builds its own entry attaches it with
/// SetTypeInfo() in its constructor, and AddComponent<T> keeps it.
template <class T, class Base = void>
constexpr ComponentTypeInfo MakeHandWrittenTypeInfo(bool declaresUpdate, const ComponentMeta* meta = nullptr,
                                                    ComponentType typeId = TypeId<T>(),
                                                    const char* name = TypeName<T>())
{
    static_assert(std::is_base_of<Component, T>::value,
                  "MakeHandWrittenTypeInfo<T>: T must derive from Deki::Component");
    return ComponentTypeInfo{ name,
                              TypeShortName<T>(),
                              typeId,
                              Detail::HandWrittenBaseId<T, Base>(),
                              std::is_abstract<T>::value,
                              declaresUpdate,
                              nullptr,
                              nullptr,
                              nullptr,
                              nullptr,
                              nullptr,
                              0,
                              meta };
}

/// The class entry AddComponent<T> attaches to a new T: the codegen's
/// registered entry, or a hand-written one for a class it never saw.
template <class T>
const ComponentTypeInfo* TypeInfoFor()
{
    static_assert(std::is_base_of<Component, T>::value, "AddComponent<T>: T must derive from Deki::Component");
    if (const ComponentTypeInfo* info = ComponentFactory::Instance().Find(TypeId<T>()))
    {
        return info;
    }
    static const ComponentTypeInfo kFallback = MakeHandWrittenTypeInfo<T>(Detail::DeclaresUpdate<T>());
    return &kFallback;
}

}  // namespace Deki
