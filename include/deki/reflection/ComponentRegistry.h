#pragma once

#include <deki/reflection/Property.h>
#include <vector>
#include <string>

#ifdef DEKI_EDITOR
#include <unordered_map>
#include <unordered_set>
#endif

// DLL export/import for ComponentRegistry (editor builds only)
// This ensures all DLLs share the same ComponentRegistry singleton
#ifdef DEKI_EDITOR
#ifdef _WIN32
#ifdef DEKI_ENGINE_EXPORTS
#define COMPONENT_REGISTRY_API __declspec(dllexport)
#else
#define COMPONENT_REGISTRY_API __declspec(dllimport)
#endif
#else
#define COMPONENT_REGISTRY_API
#endif
#else
#define COMPONENT_REGISTRY_API
#endif

/**
 * @brief Registry for component metadata
 *
 * On ESP32-S3: Minimal implementation - just provides metadata lookup by type.
 * In Editor: Full implementation with name-based lookup and enumeration.
 */

namespace Deki
{

class COMPONENT_REGISTRY_API ComponentRegistry
{
public:
    static ComponentRegistry& Instance();

    /**
     * @brief Register a component's metadata
     * @param meta Pointer to static component metadata
     */
    void Register(const ComponentMeta* meta);

    /**
     * @brief Get metadata by component type ID
     * @param typeId The ComponentType value
     * @return Pointer to metadata, or nullptr if not found
     */
    const ComponentMeta* GetMeta(uint32_t typeId) const;

    // ========================================================================
    // Compact field tables — available on EVERY platform, embedded included.
    //
    // ComponentMeta above is editor-only (it carries display names,
    // tooltips, ranges). A device build still needs to locate a field by name
    // for data-driven systems, so codegen also emits one flattened FieldRef
    // table per concrete component (own + inherited fields) and registers it
    // here unconditionally. 8 bytes per field, in flash.
    // ========================================================================

    /** @brief Register a component's flattened field table (idempotent per typeId). */
    void RegisterFields(uint32_t typeId, const FieldRef* fields, int count);

    /** @brief Look up one field by name hash. nullptr when absent. */
    const FieldRef* FindField(uint32_t typeId, uint32_t nameHash) const;

    /** @brief The whole table for a type (nullptr and outCount 0 when absent). */
    const FieldRef* GetFields(uint32_t typeId, int& outCount) const;

#ifdef DEKI_EDITOR
    /**
     * @brief Get metadata by component serialized name (GUID)
     * @param name The serialized name / GUID from scene files
     * @return Pointer to metadata, or nullptr if not found
     */
    const ComponentMeta* GetMeta(const std::string& name) const;

    /**
     * @brief Get metadata by class name (e.g., "PlatformSetupComponent")
     * @param className The C++ class name
     * @return Pointer to metadata, or nullptr if not found
     */
    const ComponentMeta* GetMetaByClassName(const std::string& className) const;

    /**
     * @brief Create a new component instance by type ID
     * @param typeId The ComponentType value
     * @return New component instance, or nullptr if not found/abstract
     */
    Component* CreateComponent(uint32_t typeId) const;

    /**
     * @brief Create a new component instance by serialized name (GUID)
     * @param name The serialized name / GUID
     * @return New component instance, or nullptr if not found/abstract
     */
    Component* CreateComponent(const std::string& name) const;

    /**
     * @brief Get all registered components
     * @return Vector of all registered component metadata
     */
    const std::vector<const ComponentMeta*>& GetAllComponents() const;

    /**
     * @brief Get components by category
     * @param category The category name (e.g., "2D")
     * @return Vector of matching component metadata
     */
    std::vector<const ComponentMeta*> GetComponentsByCategory(const std::string& category) const;

    /**
     * @brief Get all unique category names
     * @return Vector of category names
     */
    std::vector<std::string> GetCategories() const;

    /**
     * @brief Register a plugin component's metadata
     * @param meta Pointer to static component metadata
     *
     * Use this instead of Register() for plugin components so they can be
     * properly cleared during hot-reload while preserving engine components.
     */
    void RegisterPluginComponent(const ComponentMeta* meta);

    /**
     * @brief Clear all plugin-loaded components from the registry
     *
     * This is called before hot-reloading plugins. Only components registered
     * via RegisterPluginComponent() are removed; engine components are preserved.
     */
    void ClearPluginComponents();

    /**
     * @brief Check if a component type is from a plugin
     * @param typeId The ComponentType value
     * @return true if the component was registered via RegisterPluginComponent()
     */
    bool IsPluginComponent(uint32_t typeId) const;

    /**
     * @brief Unregister a component by type ID
     * @param typeId The ComponentType value
     *
     * Used during package hot-reload to clear entries BEFORE FreeLibrary,
     * preventing dangling pointers in the registry.
     */
    void Unregister(uint32_t typeId);

    /**
     * @brief Snapshot current entries as engine-core components
     *
     * Call once after engine initialization, before loading any packages.
     * Frozen entries are preserved by ClearNonEngineComponents().
     */
    void FreezeEngineComponents();

    /**
     * @brief Remove ALL entries not frozen as engine-core
     *
     * Atomically clears every package and plugin component from the registry,
     * ensuring no stale pointers survive after FreeLibrary.
     */
    void ClearNonEngineComponents();
#endif

private:
    ComponentRegistry() = default;
    ComponentRegistry(const ComponentRegistry&) = delete;
    ComponentRegistry& operator=(const ComponentRegistry&) = delete;

    std::vector<const ComponentMeta*> m_Components;

#ifndef DEKI_EDITOR
    // typeId -> meta, kept sorted by typeId so GetMeta can binary search.
    //
    // Device builds have no unordered_map here on purpose (per-node allocation
    // is expensive on an MCU), but the linear scan this replaces was O(n) on a
    // list that grows with every installed package — GetMeta is called during
    // deserialization for each component of each object, so the scan showed up
    // on scene load. A sorted vector keeps the memory profile (one contiguous
    // block, no per-entry allocation) and makes lookup O(log n).
    //
    // Only device builds maintain this: the editor already has m_ByType, and it
    // also unloads packages, which this index would have to follow.
    struct TypeIndexEntry
    {
        uint32_t typeId;
        const ComponentMeta* meta;
    };
    std::vector<TypeIndexEntry> m_TypeIndex;

    void IndexInsertOrReplace(const ComponentMeta* meta);
#endif

    // Field tables live on all platforms (see RegisterFields). The pointers are
    // static storage inside the owning package's .gen.cpp, so in the editor they
    // follow exactly the same freeze/restore lifecycle as m_Components when a
    // package DLL is unloaded.
    struct FieldTable
    {
        uint32_t typeId;
        const FieldRef* fields;
        int count;
    };
    std::vector<FieldTable> m_FieldTables;

#ifdef DEKI_EDITOR
    std::vector<FieldTable> m_FrozenFieldTables;
    std::unordered_map<uint32_t, const ComponentMeta*> m_ByType;
    std::unordered_map<std::string, const ComponentMeta*> m_ByName;
    std::unordered_set<uint32_t> m_PluginTypeIds;
    std::unordered_map<uint32_t, std::string> m_PluginTypeToName;  // typeId → serializedName for dereference-safe cleanup
    std::unordered_set<uint32_t> m_EngineTypeIds;

    // Frozen snapshots of engine-core state (captured by FreezeEngineComponents).
    // ClearNonEngineComponents restores from these instead of dereferencing
    // potentially-stale pointers from unloaded package/plugin DLLs.
    std::vector<const ComponentMeta*> m_FrozenComponents;
    std::unordered_map<uint32_t, const ComponentMeta*> m_FrozenByType;
    std::unordered_map<std::string, const ComponentMeta*> m_FrozenByName;
#endif
};

// ============================================================================
// Auto-registration helper macro (editor-only)
// ============================================================================

/**
 * @brief Automatically register a component at static initialization
 *
 * Uses GetComponentMeta() which returns a reference to a function-local static,
 * ensuring the metadata is initialized before first use (avoids SIOF).
 *
 * On embedded platforms, this macro is a no-op since:
 * - GetComponentMeta() is not declared (guarded by DEKI_EDITOR)
 * - ComponentRegistry is not needed for runtime scene loading
 *
 * Usage in .cpp file:
 *   REGISTER_COMPONENT(MyComponent)
 */
#ifdef DEKI_EDITOR
#define REGISTER_COMPONENT(ClassName)                                               \
    static struct ClassName##_Registrar                                             \
    {                                                                               \
        ClassName##_Registrar()                                                     \
        {                                                                           \
            ::Deki::ComponentRegistry::Instance().Register(&ClassName::GetComponentMeta()); \
        }                                                                           \
    } s_##ClassName##_Registrar
#else
#define REGISTER_COMPONENT(ClassName) /* No-op on embedded */
#endif

}  // namespace Deki
