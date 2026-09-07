#pragma once

/**
 * @file ComponentFactory.h
 * @brief The table of every registered component class, keyed by type id.
 *
 * Each package's generated registration hands its ComponentTypeInfo entries
 * here at startup (the engine core's, the packages', the project's). From then
 * on the scene loader creates components by id, and AddComponent<T> looks up
 * the entry to attach to the instance it constructs.
 *
 *   // Create any registered component by type id
 *   auto* comp = ComponentFactory::Instance().Create(TypeId<Game::Health>());
 */

#include <deki/Component.h>
#include <type_traits>
#include <unordered_map>

// DLL export/import for ComponentFactory (editor builds only)
// This ensures all DLLs share the same ComponentFactory singleton
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

/**
 * @brief Creates components by type id and answers "which class is this id".
 *
 * O(1) lookup for all components regardless of count.
 */
class COMPONENT_FACTORY_API ComponentFactory
{
public:
    /**
     * @brief Get singleton instance
     */
    static ComponentFactory& Instance();

    /**
     * @brief Register a component class (idempotent; a re-registration replaces
     *        the entry, which is what a hot-reloaded package needs).
     */
    void Register(const ComponentTypeInfo* info)
    {
        if (info) m_Types[info->typeId] = info;
    }

    /**
     * @brief Forget a component class (package unload).
     */
    void Unregister(uint32_t typeId)
    {
        m_Types.erase(typeId);
    }

    /**
     * @brief The class entry for a type id, or nullptr when unknown.
     */
    const ComponentTypeInfo* Find(uint32_t typeId) const
    {
        auto it = m_Types.find(typeId);
        return it != m_Types.end() ? it->second : nullptr;
    }

    /**
     * @brief Create a component by type id
     * @return New instance with its type info attached, or nullptr when the id
     *         is unknown or the class is abstract
     */
    Component* Create(uint32_t typeId) const
    {
        const ComponentTypeInfo* info = Find(typeId);
        return (info && info->create) ? info->create() : nullptr;
    }

    /**
     * @brief Check if a component type is registered
     */
    bool IsRegistered(uint32_t typeId) const
    {
        return m_Types.find(typeId) != m_Types.end();
    }

    /**
     * @brief Get number of registered components
     */
    size_t GetRegisteredCount() const
    {
        return m_Types.size();
    }

    /**
     * @brief Clear all registered components
     *
     * Useful for plugin hot-reload scenarios.
     */
    void Clear()
    {
        m_Types.clear();
    }

private:
    ComponentFactory() = default;
    std::unordered_map<uint32_t, const ComponentTypeInfo*> m_Types;
};

/**
 * @brief A class entry for a component the codegen never saw (a test double, a
 *        type defined in a .cpp). It identifies the type and says whether
 *        Update() runs; it has no deserializer, so scenes cannot load it unless
 *        `meta` supplies a createFunc of its own.
 */
namespace detail
{
template <class T, class Base>
constexpr ComponentType HandWrittenBaseId()
{
    if constexpr (std::is_void<Base>::value)
        return std::is_base_of<Behaviour, T>::value ? TypeId<Behaviour>() : TypeId<Component>();
    else
        return TypeId<Base>();
}
}  // namespace detail

/**
 * `Base` names the direct component base when it is not Deki::Behaviour /
 * Deki::Component itself (a test renderer deriving from RendererComponent), so
 * lookups by base type find the instance. A class that builds its own entry
 * attaches it with SetTypeInfo() in its constructor; AddComponent<T> keeps it.
 */
template <class T, class Base = void>
constexpr ComponentTypeInfo MakeHandWrittenTypeInfo(bool declaresUpdate, const ComponentMeta* meta = nullptr,
                                                    ComponentType typeId = TypeId<T>(), const char* name = TypeName<T>())
{
    static_assert(std::is_base_of<Component, T>::value, "MakeHandWrittenTypeInfo<T>: T must derive from Deki::Component");
    return ComponentTypeInfo{
        name,
        TypeShortName<T>(),
        typeId,
        detail::HandWrittenBaseId<T, Base>(),
        std::is_abstract<T>::value,
        declaresUpdate,
        nullptr, nullptr, nullptr, nullptr,
        nullptr, 0,
        meta
    };
}

/**
 * @brief The class entry AddComponent<T> attaches to a new T: the codegen's
 *        registered entry, or a hand-written one for a class it never saw.
 */
template <class T>
const ComponentTypeInfo* TypeInfoFor()
{
    static_assert(std::is_base_of<Component, T>::value, "AddComponent<T>: T must derive from Deki::Component");
    if (const ComponentTypeInfo* info = ComponentFactory::Instance().Find(TypeId<T>()))
        return info;
    // Whether Update() is declared cannot be known here: let a behaviour tick.
    static const ComponentTypeInfo fallback = MakeHandWrittenTypeInfo<T>(std::is_base_of<Behaviour, T>::value);
    return &fallback;
}

}  // namespace Deki
