# Deki Engine

The core of [Deki](https://github.com/dekiengine), a C++17 2D engine for
ESP32-S3 devices and desktop: a software rasterizer (RGB565 by default), a
component model driven by C++26 static-reflection codegen, an asset system,
and the provider interfaces that packages implement (display, input, render
system, memory, filesystem, time, power). Rendering, 2D content, input and
every hardware integration live in separate **packages** under
[github.com/dekiengine](https://github.com/dekiengine); this repository is
only the part every project shares.

**Status: 0.x.** File formats and the reflection ABI are versioned and may
still change before 1.0, with a migration path for every breaking change; see
the editor's [COMPATIBILITY.md](https://github.com/dekiengine/deki-editor/blob/master/COMPATIBILITY.md).
Reference documentation, generated from the code:
**https://dekiengine.github.io/docs/**

## Building

The engine is built by the editor, of which this repository is the
`deki-engine/` submodule: clone
[deki-editor](https://github.com/dekiengine/deki-editor) with
`--recurse-submodules` and follow its README. Firmware is built through the
editor's Build panel or `DekiEditor --build-firmware`. The engine's tests are
in the editor's `tests/` tree.

`project(deki-engine VERSION x.y.z)` in `CMakeLists.txt` is
`DEKI_ENGINE_VERSION`, which the editor compares with every package's
`minEngine`.

## A component

```cpp
DEKI_CATEGORY("Game")                                  // optional, above the class
DEKI_DESCRIPTION("Moves the object along X at a constant speed.")
class Mover : public Deki::Behaviour                   // deriving is the whole declaration
{
public:
    DEKI_EXPORT DEKI_RANGE(0, 10)
    float speed = 1.0f;                            // reflected: this name IS the file key

    void Update() override;
};
```

The codegen (`tools/reflection_codegen.cpp`, GCC 16.1+) emits the deserializer,
the field table, `DeclaresUpdate()` and, under `DEKI_EDITOR`, the inspector
metadata for every `DEKI_EXPORT` member. Reflected member names are the keys in
scene files: renaming one is a file-format change, done with
`DEKI_RENAMED_FROM` (see COMPATIBILITY.md).

## Layout

| Path | What it is |
| --- | --- |
| `include/deki/` | The public headers, included as `<deki/Object.h>`: `Deki::Engine` (frame loop), `Deki::Object`, `Deki::Component`/`Deki::Behaviour`, `Deki::Scene`, `Deki::SceneSystem`, `SceneMessagePack` (runtime scene format). Everything is in `namespace Deki`, unprefixed, and so are the file names |
| `include/deki/reflection/` | `Property.h` (`DEKI_EXPORT`, the attributes, the markers above a class), `ComponentRegistry`, `ComponentFactory`, `SettingsRegistry`; `TypeId.h` (a component's identity is its qualified class name) |
| `include/deki/assets/` | `AssetManager`, `AssetRef<T>`, asset packs |
| `include/deki/providers/` | The provider interfaces packages implement |
| `include/deki/interop/` | `Plugin.h` (the C exports a package DLL provides), `ReflectionAbi.h` (the ABI handshake and its changelog); the C symbols keep the `DekiPlugin_` prefix |
| `include/deki/generated/` | Codegen output: the tracked `.gen.cpp` (components need no generated header) |
| `src/` | Implementation, and the engine-internal headers (`LifecycleManager.h`, `MissingComponent.h`, `AuthoredFormat.h`, the provider registry) that only the editor's own binaries include, via the `deki-engine-internal` target |
| `tools/`, `cmake/` | The reflection code generator and its CMake driver |

## Contributing and license

See the editor repository's [CONTRIBUTING.md](https://github.com/dekiengine/deki-editor/blob/master/CONTRIBUTING.md)
and [SECURITY.md](https://github.com/dekiengine/deki-editor/blob/master/SECURITY.md).
Written and maintained by **Kirbyrawr**, with early engine work by **@imanol**.
Apache License 2.0 ([LICENSE](LICENSE)); bundled third-party code is listed in
[NOTICE](NOTICE).
