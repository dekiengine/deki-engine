#pragma once

/**
 * @file ComponentFactory.h
 * @brief Simple component factory using a single hash map
 *
 * All components (engine and plugins) register into one hash map.
 * Engine components auto-register via generated code.
 * Plugin/game components register at startup using REGISTER_RUNTIME_COMPONENT.
 *
 * Usage:
 *   // Register a component (plugins/games do this at startup)
 *   REGISTER_RUNTIME_COMPONENT(MyComponent);
 *
 *   // Create any component by type ID
 *   auto* comp = ComponentFactory::Instance().Create(HashString("MyComponent"));
 */

#include <deki/Component.h>
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


// ComponentFactory lives in Deki proper (engine 2.0); its parser sibling stays in Deki::SceneFormat.

/**
 * @brief Function pointer type for component creators
 */
using ComponentCreator = Component* (*)();

/**
 * @brief Creates components by type id (see the file comment above).
 *
 * O(1) lookup for all components regardless of count.
 * ~48 bytes memory per registered component.
 */
class COMPONENT_FACTORY_API ComponentFactory
{
public:
    /**
     * @brief Get singleton instance
     */
    static ComponentFactory& Instance();

    /**
     * @brief Register a component type for creation
     * @param typeId Hash of component class name (use ClassName::StaticType)
     * @param creator Function that creates a new instance
     */
    void Register(uint32_t typeId, ComponentCreator creator)
    {
        m_Creators[typeId] = creator;
    }

    /**
     * @brief Unregister a component type
     * @param typeId Hash of component class name
     */
    void Unregister(uint32_t typeId)
    {
        m_Creators.erase(typeId);
    }

    /**
     * @brief Create a component by type ID
     * @param typeId Hash of component class name
     * @return New component instance, or nullptr if type not found
     */
    Component* Create(uint32_t typeId)
    {
        auto it = m_Creators.find(typeId);
        if (it != m_Creators.end())
        {
            return it->second();
        }
        return nullptr;
    }

    /**
     * @brief Check if a component type is registered
     * @param typeId Hash of component class name
     * @return true if type can be created
     */
    bool IsRegistered(uint32_t typeId) const
    {
        return m_Creators.find(typeId) != m_Creators.end();
    }

    /**
     * @brief Get number of registered components
     */
    size_t GetRegisteredCount() const
    {
        return m_Creators.size();
    }

    /**
     * @brief Clear all registered components
     *
     * Useful for plugin hot-reload scenarios.
     */
    void Clear()
    {
        m_Creators.clear();
    }

private:
    ComponentFactory() = default;
    std::unordered_map<uint32_t, ComponentCreator> m_Creators;
};



/**
 * @brief Register a component type for runtime creation
 *
 * Uses static initialization to register the component.
 * Engine components use this in package-specific registration files (e.g. 2DRegistration.gen.cpp).
 * Plugins/games use this at startup for their components.
 *
 * Usage:
 *   REGISTER_RUNTIME_COMPONENT(SkyComponent);
 */
#define REGISTER_RUNTIME_COMPONENT(ClassName)                        \
    static struct ClassName##_FactoryRegistrar                       \
    {                                                                \
        ClassName##_FactoryRegistrar()                               \
        {                                                            \
            ::Deki::ComponentFactory::Instance().Register(      \
                ClassName::StaticType,                               \
                []() -> ::Deki::Component* { return new ClassName(); }); \
        }                                                            \
    } s_##ClassName##_FactoryRegistrar

}  // namespace Deki
