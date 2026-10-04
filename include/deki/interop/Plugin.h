#pragma once

/**
 * @file DekiPlugin.h
 * @brief Plugin interface for Deki engine extensions.
 *
 * Game projects create a DLL that exports these functions to register
 * custom components with the editor. The editor loads these plugins
 * at runtime when opening a project.
 *
 * Usage:
 *   1. Include this header in your game project
 *   2. Implement the required exported functions
 *   3. Build as a shared library (DLL/so/dylib)
 *   4. Place in your project's bin/ directory
 *
 * Every symbol the editor looks for is declared below, and nothing else is
 * looked for. Two things had drifted before this header was made the single
 * source of truth:
 *
 *   - It declared DekiPlugin_GetReflectionJson, GetFeatureCount and GetFeature,
 *     which all 24 packages dutifully exported and which no loader has ever
 *     resolved. Feature metadata is read from package.json, not from the DLL.
 *   - The loader resolved SetImGuiContext, GetImGuiContextSize, ClearRegistries,
 *     OnPlayModeStart and OnPlayModeStop, none of which were declared here.
 *
 * Example implementation:
 * @code
 * #include <DekiPlugin.h>
 *
 * DEKI_PLUGIN_API const char* DekiPlugin_GetName(void)    { return "MyGame"; }
 * DEKI_PLUGIN_API const char* DekiPlugin_GetVersion(void) { return "1.0.0"; }
 * DEKI_PLUGIN_API int  DekiPlugin_Init(void)              { return 0; }
 * DEKI_PLUGIN_API void DekiPlugin_Shutdown(void)          {}
 * DEKI_PLUGIN_API void DekiPlugin_RegisterComponents(void) { MyGame_Register(); }
 * @endcode
 */

// For static library builds or when building the plugin, functions are plain exports
// For DLL imports, define DEKI_PLUGIN_IMPORTS before including this header
#ifdef _WIN32
#ifdef DEKI_PLUGIN_IMPORTS
#define DEKI_PLUGIN_API __declspec(dllimport)
#elif defined(DEKI_PLUGIN_EXPORTS)
#define DEKI_PLUGIN_API __declspec(dllexport)
#else
#define DEKI_PLUGIN_API  // Static library - no import/export specifier
#endif
#else
#define DEKI_PLUGIN_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Get the plugin name for display in the editor
     * @return Human-readable plugin name (e.g., "Project-V Components")
     */
    DEKI_PLUGIN_API const char* DekiPlugin_GetName(void);

    /**
     * @brief Get the plugin version
     * @return Version string (e.g., "1.0.0")
     */
    DEKI_PLUGIN_API const char* DekiPlugin_GetVersion(void);

    /**
     * @brief Optional: Initialize the plugin
     * @return 0 on success, non-zero on failure
     */
    DEKI_PLUGIN_API int DekiPlugin_Init(void);

    /**
     * @brief Optional: Shutdown the plugin
     */
    DEKI_PLUGIN_API void DekiPlugin_Shutdown(void);

// ============================================================================
// Component Factory API (for plugins with actual component implementations)
// ============================================================================

// Forward declaration for C++ struct
#ifdef __cplusplus
    namespace Deki
    {
    struct ComponentMeta;
    }
#endif

    /**
     * @brief Get the number of components provided by this plugin
     * @return Number of components (0 if plugin only provides JSON metadata)
     */
    DEKI_PLUGIN_API int DekiPlugin_GetComponentCount(void);

/**
 * @brief Get component metadata by index
 * @param index Component index (0 to GetComponentCount()-1)
 * @return Pointer to component metadata, or nullptr if index is invalid
 *
 * The returned Deki::ComponentMeta includes a factory function that can
 * create instances of the component. This enables the editor to instantiate
 * plugin components for play mode.
 */
#ifdef __cplusplus
    DEKI_PLUGIN_API const Deki::ComponentMeta* DekiPlugin_GetComponentMeta(int index);
#else
DEKI_PLUGIN_API const void* DekiPlugin_GetComponentMeta(int index);
#endif

    /**
     * @brief Register all plugin components with the engine's Deki::ComponentRegistry
     *
     * Called by the editor after loading the plugin DLL. This allows the plugin
     * to register its components so they appear in the Add Component menu.
     */
    DEKI_PLUGIN_API void DekiPlugin_RegisterComponents(void);

    // ============================================================================
    // Optional hooks
    // ============================================================================
    //
    // Resolved with GetProcAddress/dlsym and skipped when absent, so a package only
    // implements what it needs. Declared here so the signatures are pinned: the
    // loader casts the address it finds, and a mismatched signature is a silent
    // stack corruption rather than a load failure.

    /**
     * @brief Receive the editor's ImGui context.
     *
     * Only needed by packages that call ImGui directly. Note that none
     * currently do - they draw through deki-editor's EditorUI, which shares the
     * context via Deki::SetImGuiContext.
     */
    DEKI_PLUGIN_API void DekiPlugin_SetImGuiContext(void* context);

    /**
     * @brief Report sizeof(ImGuiContext) as this package compiled it.
     *
     * The loader compares this against its own and refuses the package on a
     * mismatch, rather than letting it corrupt the shared context. A package
     * that does not export it is loaded unchecked - so export it if you touch
     * ImGui at all.
     */
    DEKI_PLUGIN_API unsigned int DekiPlugin_GetImGuiContextSize(void);

    /**
     * @brief Drop everything this package registered into engine-side registries.
     *
     * Called before the package's DLL is unloaded for a hot reload. Anything
     * holding a pointer into this DLL - factories, std::function callbacks,
     * metadata - must go here, or it dangles the moment FreeLibrary runs.
     */
    DEKI_PLUGIN_API void DekiPlugin_ClearRegistries(void);

    /// @brief Play mode is starting, for the given Deki::Scene*.
    DEKI_PLUGIN_API void DekiPlugin_OnPlayModeStart(void* scene);

    /// @brief Play mode has stopped.
    DEKI_PLUGIN_API void DekiPlugin_OnPlayModeStop(void);

#ifdef __cplusplus
}
#endif
