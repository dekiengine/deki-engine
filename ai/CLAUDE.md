# CLAUDE.md

Guidance for Claude Code (claude.ai/code) working in this repository. Where
this file disagrees with the code, the code wins; fix the file. What the engine
is, how it is built and the contribution rules are in [README.md](README.md)
and the editor's CONTRIBUTING.md / COMPATIBILITY.md; this file keeps only what
an agent needs to find its way around.

## Build

The engine is built by the editor (`deki-editor/`, of which this repository is
the `deki-engine/` submodule): `cmake --build build --target DekiEditor` there
produces `deki-engine-core`. Tests are in the editor's `tests/`.
`project(deki-engine VERSION x.y.z)` here is `DEKI_ENGINE_VERSION`, compared
with every package's `minEngine`; it always equals the editor's version
(`deki-editor/tools/ci/check_version.py`). Commit here first, push, then bump
the submodule pointer in the editor.

## Key paths

- `include/deki/`: the PUBLIC headers, `#include <deki/Object.h>` (file names are
  unprefixed like the types): `Engine.h` (frame loop), `Object.h`, `Component.h`/
  `Behaviour.h`, `Scene.h`, `SceneSystem.h`, `SceneMessagePack.h`, `ICamera.h`,
  `IClipProvider.h`/`ISortableProvider.h`. `src/` is PRIVATE to the engine
  target; its headers (`LifecycleManager.h`, `MissingComponent.h`,
  `AuthoredFormat.h`, `providers/DekiProviderRegistry*.h`) are reachable only
  through `deki-engine-internal` (editor exe, editor DLL, tests). Never put a
  header a package needs in `src/`. Consumers migrate with
  `deki-editor/tools/migrate-includes.py`.
- `include/deki/reflection/`: `Property.h` (`DEKI_EXPORT`, attributes incl.
  `DEKI_RENAMED_FROM`, the `DEKI_CATEGORY`/`DEKI_DESCRIPTION` markers that go ABOVE
  a class), `ComponentRegistry`, `ComponentFactory` (+ `TypeInfoFor<T>`),
  `SettingsRegistry`, `ObjectRef`. `include/deki/TypeId.h`: a component's id is
  the hash of its qualified class name (`TypeId<T>()`), no GUIDs since 0.11.
- `include/deki/assets/`: `AssetManager`, `AssetRef<T>`, `AssetPackReader`.
- `include/deki/providers/`: the provider interfaces (`Deki::IDisplay`,
  `IFileSystem`, `IInputSystem`, `IPackage`, `IPower`, `IRenderSystem`,
  `IMemoryProvider`, `ITimeProvider`) and their singletons.
- `include/deki/platforms/`: `esp32/`, `desktop/`; `src/platforms/editor/EditorDisplay.*` is internal.
- `include/deki/interop/`: `Plugin.h` (package DLL exports), `ReflectionAbi.h`
  (`kDekiReflectionAbiVersion` and its changelog).
- `include/deki/generated/`: codegen output; the `.gen.cpp` are tracked (no
  component header includes anything generated since 0.11).
- `include/deki/settings/`: engine-owned project settings sections.
- `entry/DekiMain.cpp`: the runtime entry point firmware and simulator builds compile.
- `tools/reflection_codegen.cpp` + `cmake/DekiReflectionCodegen.cmake`: the
  C++26 (GCC 16.1+) codegen; an unusable generator is a configure error.

## Rules that are easy to break

- **ABI.** Packages subclass `Deki::Component`, `Deki::Behaviour`, the
  provider interfaces and the editor's extension classes. Adding, removing or
  reordering a virtual on any of them, or changing the layout of
  `Deki::Object`/`Component`/`PropertyInfo`, bumps `kDekiReflectionAbiVersion`
  in the same commit and gets a line in CHANGELOG.md's ABI table;
  `deki-editor/tools/ci/check_abi.py` locks the virtual tables
  (`--update` after the bump) and `tests/core/DekiObjectLayoutTests.cpp` pins
  the sizes. Public API going away is marked `DEKI_DEPRECATED("use X")` for
  one minor version first.
- **Namespace guards.** `namespace Deki {` opens OUTSIDE any `#ifdef`
  (`DEKI_EDITOR`, `ESP32`); guarded includes stay outside the namespace and the
  guarded body goes inside it. Runtime builds (simulator, firmware) define no
  `DEKI_EDITOR`, and a namespace opened inside a false guard leaves the closing
  brace unbalanced.
- **Reflected names are the file format.** A renamed `DEKI_EXPORT` member
  keeps `DEKI_RENAMED_FROM("old")`; `DekiEditor --verify-scenes` before and after.

## Frame

`Deki::Engine` is a Meyers singleton owning `SceneSystem`, `LifecycleManager` and
the optional providers packages install. One loop serves both hosts
(`Deki::EngineHost::Device()` / `Editor()`): poll input → power tick → time →
`ProcessLifecycle` (`ProcessInternalAwake` resolves refs, then Awake, Start,
Update for behaviours whose class declares `Update` and whose `NeedsUpdate()`
holds, `RuntimeUpdate`) → callbacks → `Render()` → `PresentFrame()` (partial
present through `Deki::IDisplay::PresentRegions` when the display can). Package
systems hook the frame through `Deki::Engine::RegisterUpdate` and the generated
`deki_init_package_systems()`.

## Objects, components, scenes

`Deki::Object` = name, id, active, local transform, cached world transform,
parent/children, components. Runtime loading: MessagePack `.scene` →
`SceneMsgPackParser` → `ComponentFactory::Create(typeId)` → generated
`Deserialize()` (switch on the FNV-1a hash of the property name; aliases from
`DEKI_RENAMED_FROM` are extra cases). Unknown types become `MissingComponent`.
Editor builds load the authored JSON through `Deki::PropertyInfo`
(`Scene.cpp`, `#ifdef DEKI_EDITOR`): unknown keys are logged
(`CollectUnknownProperties`) and round-tripped; legacy keys are logged
(`CollectLegacyProperties`) and rewritten under the new name on save.
`Scene::ComponentDefaultsToJson` is what the docs build prints defaults with.

Rendering lives in the deki-rendering package (`Standard2DRenderer`,
`QuadBlit`, the golden tests); the engine only owns `IRenderSystem`,
`ICamera` and the present path.

## Adding things

- New component: a class deriving PUBLICLY from `Deki::Component`/`Behaviour`
  with public `DEKI_EXPORT` fields, `.cpp` next to it; nothing inside the class
  declares it and no generated include. Optional `DEKI_CATEGORY`/`DEKI_DESCRIPTION`
  above the class. The build regenerates reflection at configure time. Declare
  `Update()` only if needed. Reflected names AND the class name are the file format.
- New provider/platform: implement the `Deki::I*` interface in a package and
  install it from the package's `DekiPlugin_SystemInit`. ESP32-S3 and desktop
  are the targets.
