# Changelog

Notable changes to the Deki engine core. The reflection ABI between the editor
and package DLLs has its own numbered history in
`include/deki/interop/ReflectionAbi.h`, summarised here.

## Unreleased

### Changed
- The transform is a build switch. `DEKI_TRANSFORM_2D` gives every
  `Deki::Object` its x, y, rotation and scale as before; without it the object
  carries no transform at all, for a project with nothing to draw.
  `DEKI_TRANSFORM_3D` is reserved, implies 2D, and is an error without it. Every
  build defines 2D in this release, so nothing changes yet; the generated
  project builds will set it from what the project's packages declare.
- Reflection ABI 10: the handshake carries the transform width, so a package
  DLL built at a different width is refused rather than reading the wrong
  offsets.

## 0.13.0

### Breaking
- One base class. `Deki::Behaviour` is folded into `Deki::Component`: every
  component may declare `Awake()`, `Start()` and `Update()`, and one that
  declares none is data. `Component::AsBehaviour()` and `<deki/Behaviour.h>`
  are gone; `Scene::GetBehaviours()` is `GetActiveComponents()`.
  `tools/migrate-components.py` rewrites `Deki::Behaviour` to
  `Deki::Component`. Reflection ABI 9; every package is re-released as 0.13.0
  with `minEngine` 0.13.0.

## 0.12.3

### Changed
- `AddComponent<T>()` is the only `AddComponent` a script can see. The
  by-name and pre-built-instance forms are private to `Deki::Object`, reached
  through the engine-internal `Deki::Internal` entry points by the scene
  loaders and the editor; package editor code that builds objects on the
  user's behalf calls `DekiEditor::AddComponentByName()` from the editor DLL.

## 0.12.2

### Changed
- `AddComponent<T>()` is the one way a script adds a component. The by-id and
  device-side by-name forms added in 0.12.1 are withdrawn; the by-name form
  stays editor-only, for the editor's own commands and the scene loader.

## 0.12.1

### Added
- `Object::AddComponent(Deki::TypeId<T>())` and `AddComponent("Game::Health")`
  on every platform; the component factory is engine-internal. (Withdrawn in
  0.12.2.)

## 0.12.0

### Breaking
- `Deki::Behaviour` lost `NeedsUpdate()`, `SetNeedsUpdate()` and the flag
  behind them. `LifecycleManager` calls `Update()` on exactly the behaviours
  whose class declares it (`ComponentTypeInfo::declaresUpdate`). Reflection
  ABI 8.

### Added
- `MakeHandWrittenTypeInfo<T, Base>()` names the direct component base, and
  `AddComponent<T>` keeps an entry the constructor attached, so a hand-written
  class (a test renderer deriving from RendererComponent) is found by base
  type like a reflected one.

## 0.11.1

### Fixed
- `DekiReflectionCodegen.cmake` reflects, for a project plugin, every header
  with a derived class (`: public X`), so a component whose base is another
  project component is found even when the header never spells `Component`
  or `Behaviour`. Package and engine headers keep the narrower rule (the
  engine's ESP32 platform headers need ESP-IDF).

## 0.11.0

### Breaking
- Macro-free components. `Deki::Component` no longer has per-class virtuals:
  `GetType`, `GetBaseType`, `DeclaresUpdate`, `Deserialize`, `ResolveObjectRefs`
  and `RemapObjectRefs` are plain calls through the instance's
  `ComponentTypeInfo` (`Component::GetTypeInfo()`), attached by
  `AddComponent<T>`, the factory and the scene loader. `DEKI_COMPONENT`,
  `DECLARE_COMPONENT_TYPE`, `REGISTER_COMPONENT`, `REGISTER_RUNTIME_COMPONENT`
  and `DEKI_REGISTER_PROJECT_SETTINGS*` are gone; registration is generated.
- A component's identity is its qualified class name: `Deki::TypeId<T>()`
  (`include/deki/TypeId.h`, computed from the compiler's function signature,
  no RTTI) replaces `T::StaticType` and the declared GUID;
  `ComponentMeta::serializedName` holds that name. `ComponentFactory` stores
  `ComponentTypeInfo` entries (`Register(info)`, `Find(typeId)`).
- The reflection codegen finds components by their base class in any namespace,
  reads `DEKI_CATEGORY` / `DEKI_DESCRIPTION` / `DEKI_DISPLAY_NAME` /
  `DEKI_SETTINGS_SECTION` from the lines above the class, and emits no per-type
  `.gen.h`. `ComponentMeta` gained `settingsSection` and `typeInfo` (appended).
- Reflection ABI 7.

### Added
- `Deki::MakeHandWrittenTypeInfo<T>()` for hand-written component classes
  (tests); `AddComponent<T>` falls back to a minimal entry for a class the
  codegen never saw.
- Editor builds map 0.10 component GUIDs to class names
  (`src/reflection/LegacyTypeNames`) when loading scenes, `ObjectRef` ids and
  `PropertyRef` data.

## 0.10.0

### Breaking
- The engine carries the editor's version number from here on (0.10.0 ships with editor
  0.10.0); a package's `minEngine` names that number. The 0.x major is deliberate:
  the API is not declared stable yet.
- Every engine type lives in `namespace Deki` without the `Deki` prefix
  (`DekiObject` → `Deki::Object`, `IDekiDisplay` → `Deki::IDisplay`,
  `DekiHashString` → `Deki::HashString`); the formerly global `Scene`,
  `ComponentRegistry`, `ObjectRef`, `SetupComponent` and the rest moved in
  unchanged. Macros and `src/interop/` keep their names. Symbol mangling
  changed, so every package rebuilds; the reflection ABI version is bumped so a
  1.x DLL is refused with a message. Migration: `deki-editor/tools/migrate-namespace.py`.
- The reflection codegen reflects both the global namespace and `namespace
  Deki`, emits each class's definitions inside its own namespace, and qualifies
  the engine names it writes.
- Public headers live in `include/deki/` and are included as
  `<deki/Object.h>`, `<deki/reflection/Property.h>`,
  `<deki/providers/IDisplay.h>` (the `Deki` file-name prefix is gone, as the
  type prefix already was). `src/` is PRIVATE to the engine target; the editor's
  own binaries reach the internal headers there through the
  `deki-engine-internal` interface target, packages cannot. Generated files
  (`.gen.h` and the tracked `.gen.cpp`) live in `include/deki/generated/` and
  are included as `"deki/generated/X.gen.h"`; the codegen emits the new
  spellings. Migration for consumers: `deki-editor/tools/migrate-includes.py`.

### Fixed
- Runtime builds (simulator, firmware: no `DEKI_EDITOR`) failed to compile
  since the namespace release: the wrap placed `namespace Deki {` inside an
  `#ifdef DEKI_EDITOR` (or `#ifdef ESP32`) block and the closing brace outside
  it, so `Scene.h`, `reflection/Serialization.h`, `Scene.cpp`, `Engine.cpp`,
  `SetupComponent.cpp`, the ESP32 providers and the editor display all had an
  unbalanced namespace whenever their guard was false. The namespace now opens
  outside the guard in every one of them.

### Added
- `DEKI_DEPRECATED("use X")` (`DekiExport.h`): a public API marked in minor
  version N is removed in N+1 (`deki-editor/COMPATIBILITY.md`). The virtual
  tables of every class packages subclass are locked by
  `deki-editor/tools/ci/check_abi.py`; a change to them without a
  `kDekiReflectionAbiVersion` bump fails the editor's CI.

## 1.3.x

### Added
- `Scene::ComponentPropertiesToJson` / `ComponentDefaultsToJson`: a
  component's reflected properties as the JSON a scene file holds, used by the
  documentation build to print defaults.
- `formatVersion` written into authored scene JSON; absent means 1; a newer
  file is refused with a message.
- `DEKI_RENAMED_FROM("old")` on reflected members: the codegen emits the old
  key as an alias, the editor loader accepts it and records it as a legacy key.

### Changed
- Reflection codegen: a generator that fails to compile (GCC older than 16.1)
  or to run stops the configure with a message, instead of a warning followed
  by stale metadata.
- Repository moved to github.com/dekiengine/deki-engine.

## Reflection ABI history

| Version | Change |
| --- | --- |
| 6 | Engine 0.10: every engine type moved into `namespace Deki`. Every `sizeof` is unchanged, but the mangled name of every exported engine function changed, so a pre-0.10 package would fail with unresolved imports; the bump routes it to the loader's "built against another engine, rebuilding" path instead. |
| (no bump) | `Deki::PropertyInfo` grew `renamedFrom` (`DEKI_RENAMED_FROM`). The size change is caught by `sizeofPropertyInfo` on its own: a package built before it is refused and rebuilt. |
| 5 | Added `DekiComponent::DeclaresUpdate()`; a vtable slot in the middle, so packages built against 4 are refused. |
| 4 | Dropped the deprecated `getType()`/`getBaseType()` forwarders (layout-compatible, semantically not). |
| 3 | Added `sizeofDekiObject` and `sizeofDekiComponent` to the handshake. |
| 2 | Dropped `sizeofNodeMeta`; node metadata moved to the deki-nodegraph package. |
| 1 | First handshake: `version` plus the `sizeof` of `PropertyInfo`, `ComponentMeta`, `FieldRef` and the node metadata. |
