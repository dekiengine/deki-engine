#pragma once

// The functions a package DLL exports so the editor can load it. A game
// project or package builds a shared library (DLL/so/dylib) that implements
// them, placed in the project's bin/ directory; the editor loads it when it
// opens the project and registers its components.
//
// This header is the complete list: every symbol the editor looks for is
// declared here, and it looks for nothing else. Feature metadata comes from
// package.json, not from the DLL.
//
//   #include <deki/interop/Plugin.h>
//
//   DEKI_PLUGIN_API const char* DekiPluginGetName(void)    { return "MyGame"; }
//   DEKI_PLUGIN_API const char* DekiPluginGetVersion(void) { return "1.0.0"; }
//   DEKI_PLUGIN_API int  DekiPluginInit(void)              { return 0; }
//   DEKI_PLUGIN_API void DekiPluginShutdown(void)          {}
//   DEKI_PLUGIN_API void DekiPluginRegisterComponents(void) { MyGame_Register(); }

// Define DEKI_PLUGIN_EXPORTS when building the plugin and DEKI_PLUGIN_IMPORTS
// when importing it; with neither, it is a static library with no specifier.
#ifdef _WIN32
#ifdef DEKI_PLUGIN_IMPORTS
#define DEKI_PLUGIN_API __declspec(dllimport)
#elif defined(DEKI_PLUGIN_EXPORTS)
#define DEKI_PLUGIN_API __declspec(dllexport)
#else
#define DEKI_PLUGIN_API  // static library
#endif
#else
#define DEKI_PLUGIN_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    /// The plugin's name as the editor shows it.
    DEKI_PLUGIN_API const char* DekiPluginGetName(void);

    /// The plugin's version, such as "1.0.0".
    DEKI_PLUGIN_API const char* DekiPluginGetVersion(void);

    /// Optional. Starts the plugin; returns 0 on success.
    DEKI_PLUGIN_API int DekiPluginInit(void);

    /// Optional. Shuts the plugin down.
    DEKI_PLUGIN_API void DekiPluginShutdown(void);

    // ============================================================================
    // Component factory API, for plugins that implement components
    // ============================================================================

#ifdef __cplusplus
    namespace Deki
    {
    struct ComponentMeta;
    }
#endif

    /// Number of components the plugin provides.
    DEKI_PLUGIN_API int DekiPluginGetComponentCount(void);

/// Metadata of component `index` (0 to count - 1), or nullptr when out of
/// range. It includes a factory, so the editor can create the component in
/// play mode.
#ifdef __cplusplus
    DEKI_PLUGIN_API const Deki::ComponentMeta* DekiPluginGetComponentMeta(int index);
#else
DEKI_PLUGIN_API const void* DekiPluginGetComponentMeta(int index);
#endif

    /// Registers the plugin's components with the engine, so they appear in
    /// the Add Component menu. The editor calls it after loading the DLL.
    DEKI_PLUGIN_API void DekiPluginRegisterComponents(void);

    // ============================================================================
    // Optional hooks
    // ============================================================================
    //
    // Found with GetProcAddress/dlsym and skipped when absent, so a package
    // implements only what it needs. Declared here to fix the signatures: the
    // loader casts the address it finds, and a wrong signature corrupts the
    // stack silently instead of failing to load.

    /// Receives the editor's ImGui context. Only packages that call ImGui
    /// directly need it; packages that draw through deki-editor's EditorUI get
    /// the context through Deki::SetImGuiContext.
    DEKI_PLUGIN_API void DekiPluginSetImGuiContext(void* context);

    /// sizeof(ImGuiContext) as this package compiled it. The loader refuses
    /// the package when it differs from its own, rather than let it corrupt
    /// the shared context. A package without it loads unchecked, so export it
    /// if you touch ImGui at all.
    DEKI_PLUGIN_API unsigned int DekiPluginGetImGuiContextSize(void);

    /// Removes everything this package registered in engine registries. Called
    /// before the DLL is unloaded for a hot reload: anything holding a pointer
    /// into the DLL (factories, std::function callbacks, metadata) must go
    /// here, or it dangles once FreeLibrary runs.
    DEKI_PLUGIN_API void DekiPluginClearRegistries(void);

    /// Play mode is starting, with the given Deki::Scene*.
    DEKI_PLUGIN_API void DekiPluginOnPlayModeStart(void* scene);

    /// Play mode has stopped.
    DEKI_PLUGIN_API void DekiPluginOnPlayModeStop(void);

#ifdef __cplusplus
}
#endif
