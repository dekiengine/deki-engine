#pragma once

#include <deki/reflection/Property.h>
#include <deki/Component.h>
#include <vector>
#include <string>

#ifdef DEKI_EDITOR
#include <unordered_map>
#include <unordered_set>
#endif

// DLL export/import in editor builds, like ComponentRegistry: the singleton
// lives in deki-engine-core.dll and all package DLLs share it.
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

// SettingsTypeInfo is one registered project-settings type and its live
// instance.

namespace Deki
{

struct SettingsTypeInfo
{
    const char* sectionName;    // "Rendering", "Audio": editor section header and registry key
    const ComponentMeta* meta;  // component metadata (properties, factory, etc.)
    Component* instance;        // owned by the registry, lives as long as the plugin DLL
    bool isPlugin;              // true: cleared on plugin unload
};

/// Registry of project-settings types, which packages can add to.
///
/// Packages declare settings types as ordinary components with a
/// DEKI_SETTINGS_SECTION("...") marker above the class, and the codegen
/// registers them here from the package's registration function. The
/// Project Settings panel walks the registry and draws each type's properties
/// with PropertyEditorFactory, so tooltips, ranges and visibility conditions
/// all work.
///
/// Unlike ComponentRegistry, which holds only metadata, this registry owns
/// one live instance per type. Plugin instances are deleted by
/// ClearPluginSettings before FreeLibrary, so no vtable is left dangling.
class SETTINGS_REGISTRY_API SettingsRegistry
{
public:
    static SettingsRegistry& Instance();

    /// Registers a settings type owned by deki-engine-core. `sectionName` is
    /// the panel's section header and lookup key (e.g. "Rendering"). `meta`
    /// must have a working createFunc.
    void Register(const char* sectionName, const ComponentMeta* meta);

    /// Registers a settings type owned by a plugin or package DLL. Its instance
    /// is deleted by ClearPluginSettings() or Unregister() before the DLL
    /// unloads, so no vtable is left dangling.
    void RegisterPlugin(const char* sectionName, const ComponentMeta* meta);

    /// The settings instance for a section name.
    Component* GetByName(const char* sectionName);

    /// The settings instance for a component type id.
    Component* GetByTypeId(uint32_t typeId);

    /// The instance of settings type T, which must be registered.
    template <typename T>
    T* Get()
    {
        return static_cast<T*>(GetByTypeId(::Deki::TypeId<T>()));
    }

    /// All registered settings types, for the Project Settings panel.
    const std::vector<SettingsTypeInfo>& GetAll() const { return m_All; }

    /// Deletes every plugin settings instance and keeps the engine-core ones.
    /// PackageLoader calls it before FreeLibrary on plugin DLLs.
    void ClearPluginSettings();

    /// Removes a settings type. Called when a single package unloads, before
    /// FreeLibrary, so its stale pointers cannot crash the editor when another
    /// package reloads.
    void Unregister(uint32_t typeId);

    /// True if the type was registered with RegisterPlugin, not Register.
    bool IsPluginSetting(uint32_t typeId) const;

    /// Saves the current state as the engine-core baseline. Call once after
    /// engine init, before any package loads. ClearPluginSettings restores from
    /// it instead of dereferencing possibly stale meta pointers.
    void FreezeEngineSettings();

    /// Writes every plugin-owned settings instance to a JSON string.
    ///
    /// Hot reload uses it to keep the user's edits: snapshot before unloading
    /// plugin DLLs, then RestoreFromJson() once the DLLs have reloaded and
    /// registered fresh default instances. Engine-core settings are left out;
    /// they live in deki-engine-core.dll and survive the reload.
    ///
    /// The format matches deki.json's "packageSettings" block, so a snapshot
    /// can also be saved to disk.
    std::string SnapshotPluginSettingsAsJson() const;

    /// Restores plugin settings from a SnapshotPluginSettingsAsJson() snapshot.
    ///
    /// Sections whose package did not reload are skipped and can be restored on
    /// the next reload. Properties the new metadata no longer has are skipped,
    /// so renamed or removed fields do no harm.
    void RestoreFromJson(const std::string& snapshotJson);

private:
    SettingsRegistry() = default;
    SettingsRegistry(const SettingsRegistry&) = delete;
    SettingsRegistry& operator=(const SettingsRegistry&) = delete;

    std::vector<SettingsTypeInfo> m_All;

#ifdef DEKI_EDITOR
    std::unordered_map<std::string, size_t> m_ByName;  // section name -> index in m_All
    std::unordered_map<uint32_t, size_t> m_ByTypeId;
    std::unordered_set<uint32_t> m_PluginTypeIds;
    std::unordered_set<uint32_t> m_EngineTypeIds;

    // Frozen baseline, as in ComponentRegistry.
    std::vector<SettingsTypeInfo> m_FrozenAll;
    std::unordered_map<std::string, size_t> m_FrozenByName;
    std::unordered_map<uint32_t, size_t> m_FrozenByTypeId;
#endif
};

}  // namespace Deki
