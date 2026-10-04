// The reflection-layout handshake, compiled into EVERY package DLL.
//
// BuildFileGenerator appends this file to every package target and to the
// game plugin, so each DLL reports the reflection struct layout it was built
// against. PackageLoader and PluginLoader compare that with the editor's and
// refuse a mismatch (ReflectionAbi.h says why a stale DLL is so destructive).
//
// Editor-only: on a device everything is linked statically, so there is no
// loader and no ABI boundary to guard. The firmware build globs the engine's
// src tree, so this file compiles to nothing there.

#ifdef DEKI_EDITOR

#include <deki/interop/ReflectionAbi.h>

#ifdef _WIN32
#define DEKI_ABI_EXPORT __declspec(dllexport)
#else
#define DEKI_ABI_EXPORT __attribute__((visibility("default")))
#endif

extern "C"
{
    // This DLL's compiled reflection layout.
    //
    // Not DEKI_PLUGIN_API: that macro exports only when the target defines
    // DEKI_PLUGIN_EXPORTS, and this must be exported from every package, or
    // the guard silently does nothing.
    DEKI_ABI_EXPORT const DekiReflectionAbi* DekiPluginGetReflectionAbi()
    {
        // Function-local static: filled on first call from THIS DLL's headers, and
        // its address stays valid for as long as the DLL is loaded.
        static const DekiReflectionAbi abi = DekiCurrentReflectionAbi();
        return &abi;
    }

}  // extern "C"

#endif  // DEKI_EDITOR
