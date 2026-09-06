/**
 * @file DekiPackageAbi.cpp
 * @brief The reflection-layout handshake, compiled into EVERY package DLL.
 *
 * BuildFileGenerator appends this file to every package target and to the
 * game plugin, so each DLL reports the reflection struct layout it was
 * actually built against. PackageLoader and
 * PluginLoader compare that against the editor's and refuse a mismatch (see
 * DekiReflectionAbi.h for why a stale DLL is so destructive).
 *
 * Editor-only by design: on device everything is statically linked, there is no
 * loader and no ABI boundary to guard, so this compiles to nothing there (the
 * firmware build globs the engine's src tree, which would otherwise pull it in).
 */

#ifdef DEKI_EDITOR

#include <deki/interop/ReflectionAbi.h>

#ifdef _WIN32
#define DEKI_ABI_EXPORT __declspec(dllexport)
#else
#define DEKI_ABI_EXPORT __attribute__((visibility("default")))
#endif

extern "C"
{
    /**
     * @brief This DLL's compiled reflection layout.
     *
     * Deliberately not using DEKI_PLUGIN_API: that macro only expands to an export
     * when the target happens to define DEKI_PLUGIN_EXPORTS, and this must be
     * exported from every package unconditionally or the guard silently does nothing.
     */
    DEKI_ABI_EXPORT const DekiReflectionAbi* DekiPlugin_GetReflectionAbi()
    {
        // Function-local static: filled on first call from THIS DLL's headers, and
        // its address stays valid for as long as the DLL is loaded.
        static const DekiReflectionAbi abi = DekiCurrentReflectionAbi();
        return &abi;
    }

}  // extern "C"

#endif  // DEKI_EDITOR
