#pragma once

/// DLL export/import annotation for engine-core symbols.
///
/// Kept apart from Engine.h so headers that need only the annotation (such as
/// Scene.h) do not include Engine.h, which includes Scene.h back.
///
/// Expands to nothing outside the editor build: runtime and embedded targets
/// link the engine statically, so there is no import library.
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

/// Marks a public API that goes away in the next minor version:
///
///     DEKI_DEPRECATED("use Object::GetChildren()") ChildList Children() const;
///
/// Policy (deki-editor/COMPATIBILITY.md): a public function, type or macro is
/// marked in minor N and removed in N+1, so every package gets one release of
/// warnings before it stops compiling. The message names the replacement.
/// Deprecating a virtual leaves the vtable alone; removing it later changes
/// it, so that removal bumps kDekiReflectionAbiVersion.
///
/// The reflection codegen ignores this attribute; a generated file that
/// touches a deprecated member warns like any other caller.
#define DEKI_DEPRECATED(msg) [[deprecated(msg)]]
