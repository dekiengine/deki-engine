#pragma once

/**
 * @brief DLL export/import annotation for engine-core symbols.
 *
 * Split out of DekiEngine.h so that headers needing only the annotation —
 * Scene.h, for one — do not have to include DekiEngine.h, which includes
 * Scene.h right back. That cycle survived only on #pragma once and made the
 * two headers order-dependent.
 *
 * Expands to nothing outside the editor build: runtime and embedded targets
 * link the engine statically, so there is no import library to annotate.
 */
#ifdef DEKI_EDITOR
#ifdef _WIN32
#ifdef DEKI_ENGINE_EXPORTS
#define DEKI_ENGINE_API __declspec(dllexport)
#else
#define DEKI_ENGINE_API __declspec(dllimport)
#endif
#else
#define DEKI_ENGINE_API
#endif
#else
#define DEKI_ENGINE_API
#endif

/**
 * @brief Marks a public API that goes away in the next minor version.
 *
 *     DEKI_DEPRECATED("use Object::GetChildren()") ChildList Children() const;
 *
 * The policy (deki-editor/COMPATIBILITY.md): a public function, type or
 * macro is marked in minor N and removed in N+1, so every package gets one
 * release of warnings before it stops compiling. The message names the
 * replacement. Deprecating a virtual does not change the vtable; removing it
 * later does, and that removal bumps kDekiReflectionAbiVersion.
 *
 * The C++26 reflection codegen reads only its own annotations, so the
 * attribute is invisible to it; a generated file that touches a deprecated
 * member warns like any other caller.
 */
#define DEKI_DEPRECATED(msg) [[deprecated(msg)]]
