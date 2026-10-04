#pragma once

#include <deki/reflection/Property.h>
#include <deki/Component.h>
#include <vector>
#include <string>

#ifdef DEKI_EDITOR
#include <unordered_map>
#include <unordered_set>
#endif

// DLL export/import for SettingsRegistry (editor builds only).
// Mirrors ComponentRegistry - the singleton lives in deki-engine-core.dll
// and is shared across all package DLLs.
#ifdef DEKI_EDITOR
#ifdef _WIN32
#ifdef DEKI_ENGINE_EXPORTS
#define SETTINGS_REGISTRY_API __declspec(dllexport)
#else
#define SETTINGS_REGISTRY_API __declspec(dllimport)
#endif
#else
#define SETTINGS_REGISTRY_API
#endif
#else
#define SETTINGS_REGISTRY_API
#endif

/**
 * @brief One registered project-settings type plus its live instance.
 */

namespace Deki
{

struct SettingsTypeInfo
{
    const char* sectionName;    // "Rendering", "Audio" - editor section header & registry key
    const ComponentMeta* meta;  // component metadata (properties, factory, etc.)
    Component* instance;        // owned by registry, lifetime tied to plugin DLL
    bool isPlugin;              // true → cleared on plugin unload
};

/**
 * @brief Registry for package-extensible project-settings types.
 *
 * Packages declare settings types as ordinary components with a
 * DEKI_SETTINGS_SECTION("...") marker above the class; the reflection codegen
 * registers them here from the package's registration function. The editor's Project Settings panel iterates the
 * registry and renders each type's properties via the existing PropertyEditorFactory machinery - tooltips, ranges,
 * visibility conditions all work out of the box.
 *
 * The registry **owns** the live instance (one per registered type), unlike
 * ComponentRegistry which only owns metadata. Plugin instances are deleted
 * before FreeLibrary via ClearPluginSettings to avoid dangling vtables.
 */
class SETTINGS_REGISTRY_API SettingsRegistry
{
public:
    static SettingsRegistry& Instance();

    /**
     * @brief Register a settings type owned by deki-engine-core.
     * @param sectionName UI section header & lookup key (e.g. "Rendering")
     * @param meta Component metadata; must contain a working createFunc.
     */
    void Register(const char* sectionName, const ComponentMeta* meta);

    /**
     * @brief Register a settings type owned by a plugin/package DLL.
     *
     * Live instance is deleted by ClearPluginSettings() / Unregister() before
     * the owning DLL is unloaded, preventing dangling-vtable crashes.
     */
    void RegisterPlugin(const char* sectionName, const ComponentMeta* meta);

    /// Look up a settings instance by section name.
    Component* GetByName(const char* sectionName);

    /// Look up a settings instance by component type id (uint32_t hash of GUID).
    Component* GetByTypeId(uint32_t typeId);

    /// Type-safe lookup: T must be a registered settings type.
    template <typename T>
    T* Get()
    {
        return static_cast<T*>(GetByTypeId(::Deki::TypeId<T>()));
    }

    /// All registered settings types (for the editor panel walk).
    const std::vector<SettingsTypeInfo>& GetAll() const { return m_All; }

    /**
     * @brief Clear all plugin-loaded settings instances.
     *
     * Called by PackageLoader before FreeLibrary on plugin DLLs. Engine-core
     * registrations are preserved.
     */
    void ClearPluginSettings();

    /**
     * @brief Remove a settings type by component type id.
     *
     * Used during per-package unload to clear entries before FreeLibrary,
     * preventing dangling pointers from one package taking down the editor
     * when another is reloaded.
     */
    void Unregister(uint32_t typeId);

    /// True if the type was registered via RegisterPlugin (vs Register).
    bool IsPluginSetting(uint32_t typeId) const;

    /**
     * @brief Snapshot current state as engine-core baseline.
     *
     * Call once after engine init, before any package loads. ClearPluginSettings
     * uses the snapshot to restore instead of dereferencing potentially-stale
     * meta pointers.
     */
    void FreezeEngineSettings();

    /**
     * @brief Snapshot every plugin-owned settings instance to a JSON string.
     *
     * Used by hot reload: capture the user's edits before unloading plugin
     * DLLs, then restore them via RestoreFromJson() after the DLLs reload and
     * re-register their (default-valued) instances.
     *
     * Engine-core settings are NOT included - those instances survive the
     * reload because they live in deki-engine-core.dll.
     *
     * The format mirrors deki.json's "packageSettings" block, so a snapshot can
     * also be persisted to disk and restored later if needed.
     */
    std::string SnapshotPluginSettingsAsJson() const;

    /**
     * @brief Restore plugin settings from a snapshot produced by
     *        SnapshotPluginSettingsAsJson().
     *
     * Sections whose owning package didn't reload are silently skipped (the
     * user can still recover them on the next reload). Properties that no
     * longer exist on the new metadata are silently skipped - graceful for
     * field renames or removals.
     */
    void RestoreFromJson(const std::string& snapshotJson);

private:
    SettingsRegistry() = default;
    SettingsRegistry(const SettingsRegistry&) = delete;
    SettingsRegistry& operator=(const SettingsRegistry&) = delete;

    std::vector<SettingsTypeInfo> m_All;

#ifdef DEKI_EDITOR
    std::unordered_map<std::string, size_t> m_ByName;  // section name → index in m_All
    std::unordered_map<uint32_t, size_t> m_ByTypeId;
    std::unordered_set<uint32_t> m_PluginTypeIds;
    std::unordered_set<uint32_t> m_EngineTypeIds;

    // Frozen baseline (mirrors ComponentRegistry pattern at lines 170-175).
    std::vector<SettingsTypeInfo> m_FrozenAll;
    std::unordered_map<std::string, size_t> m_FrozenByName;
    std::unordered_map<uint32_t, size_t> m_FrozenByTypeId;
#endif
};

}  // namespace Deki
