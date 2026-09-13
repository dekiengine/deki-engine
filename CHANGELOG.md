# Changelog

Notable changes to the Deki engine core. The reflection ABI between the editor
and package DLLs has its own numbered history in
`include/deki/interop/ReflectionAbi.h`, summarised here.

## Unreleased

### Added
- `Deki::Vector<T>`: a growable array whose growth can fail. `std::vector`
  cannot — `push_back` calls `operator new`, which with exceptions off has no
  way to report exhaustion and aborts. Everything here that can allocate
  returns bool: `Reserve` once up front makes the pushes that follow
  infallible, a failed growth leaves everything already stored intact, and
  `RemoveUnordered` is the O(1) swap-with-last a list of live entities wants.
  Growth doubles from a first block of 4, not 16, because a device project
  has many short lists. Same trivially-copyable `T` as `Buffer`.
- `IMemoryProvider::AllocateRaw`, `FreeRaw` and `GetRawBlockSize`: the plain
  natural-alignment allocator the global `operator new` routes through, so
  every allocation on a device really does reach the provider and a board can
  substitute its heap for `std::vector` and scalar `new` too. Measured at
  46-51 ns/op against 47-52 for malloc+free — free, within noise. Counting
  the bytes is separate and opt-in behind `DEKI_MEMORY_TRACK_RAW` (63-71
  ns/op, +35%), because with no header the block size has to come from the
  heap and on Windows that is `_msize` taking a lock.
- Global `operator new`/`delete` on builds with exceptions off
  (`src/providers/OperatorNew.cpp`), which is the half `Deki::Buffer` cannot
  reach: `std::vector` and `std::string` call `operator new` internally, so
  their growth failures were the last silent reboot. A failure now says how
  many bytes it wanted before it aborts, and `std::nothrow` works —
  `new (std::nothrow) T[n]` returns null instead of aborting inside a
  try/catch that `-fno-exceptions` deleted. Instrumentation, not a re-route:
  still `malloc`, same alignment, no header, because routing these through
  `Memory::Allocate` would add 16 bytes to every `std::string` that outgrows
  its SSO buffer and memset memory a vector is about to overwrite. Compiled
  only where `__cpp_exceptions` is undefined, so host builds are untouched;
  the unused overloads are garbage-collected, and the firmware is the same
  size as before.
- `Deki::Memory::LogStats(label)`: allocated and free, both regions, in one
  line, plus live and peak counts in a debug build. Answering "how much
  memory am I using?" needed `GetStats` for one half and a provider the
  caller does not have for the other. `GetAvailableInternal()` fills in the
  companion `GetAvailablePSRAM` had been missing.
- `Memory::Initialize` logs the free internal and external RAM once, and the
  size at which `Auto` goes external, so a project can be sized against real
  numbers instead of against its first out-of-memory line.
- `Deki::Buffer<T>`: an owning array that allocates through `Deki::Memory` and
  frees itself. This is what component and package code reaches for instead of
  an allocator call plus a matching `Free`. Every memory bug this engine has
  actually had was an ownership bug rather than a churn bug — a `delete[]`
  paired with a `Deki::Memory` allocation, a buffer nothing freed, a cleanup
  path that released something a borrowing function did not own, a null pointer
  that reached a `memcpy` because one error branch forgot to return — and a
  destructor cannot make any of those mistakes. Move-only; `T` must be
  trivially copyable and trivially destructible, both enforced by
  `static_assert` because nothing here constructs or destroys elements.
- `Deki::MemoryUse`: what an allocation is FOR — `Hot` (read or written every
  frame), `Buffer` (large and read-mostly), `Dma` (a peripheral has to reach
  it), or `Auto`. Callers say how the memory will be used and the engine
  decides where it lives, because a caller cannot know whether this board has
  external RAM or how slow it is. The old `bool prefer_psram` overload remains
  as a header inline mapping to `Buffer`/`Auto`.
- `Deki::Memory::AllocateArray<T>`: a checked replacement for `new T[n]` for
  any count that comes from a file, a scene, or the screen. `new[]` cannot fail
  gracefully on the device — ESP-IDF builds with exceptions off, so the
  `bad_alloc` reaches `__cxa_allocate_exception` and aborts, rebooting the
  board, and `std::nothrow` aborts identically because libstdc++ implements it
  by calling the throwing form inside a `try`/`catch` that `-fno-exceptions`
  has removed. The multiplication is checked too: `count * sizeof(T)` is a
  32-bit `size_t` on the device, so a corrupt count silently wraps small.
- `IMemoryProvider::AllocateDma`, and `MALLOC_CAP_DMA` behind it on the ESP32.
  `MemoryUse::Dma` had documented a promise the allocator was not keeping.

### Changed
- **`MemoryUse` is required and `Auto` is gone.** Placement is never inferred
  and never defaulted: every allocating call names its region, and omitting
  it does not compile. `Auto` used to send anything at or above 16 KB to
  PSRAM, which has the sign backwards in a graphics engine — the largest
  allocations are usually the ones touched every frame, and a framebuffer in
  PSRAM measures markedly worse than one in internal RAM on real ESP32-S3
  hardware. `Internal` replaces it as a value you ask for rather than one you
  get by omission; `Buffer` is the only route to external RAM.
  `kLargeAllocation` is gone with the rule it served, and `Deki::Vector` now
  takes its region as a constructor argument with no default constructor.
  Every call site in the engine and all 25 packages already passed one
  explicitly, so nothing needed changing but the signatures.
- `Memory::Shutdown` clears the external-RAM flags. They outlived the backend
  that reported them, so after a shutdown the engine still believed PSRAM was
  present and a later `MemoryUse::Buffer` failed with "Failed to allocate N
  bytes in external RAM" on a machine with gigabytes free.
- C++23 (`CMAKE_CXX_STANDARD 23`) instead of 17. ESP-IDF forces
  `-std=gnu++2b` for chip targets unconditionally, so firmware had been
  compiling at C++23 while the engine, the editor and the generated project
  builds were pinned at 17 — the same package source built at two standards
  depending on target, so a C++20 feature would compile for the board and
  fail on the desktop. Two identifiers named `requires` had to be renamed
  (it is a keyword since C++20) and nothing else broke.
- Memory placement resolves once and is not revisited: there is no fallback to
  the other region when the chosen one is full. Quietly moving a `Hot` buffer
  into external RAM turns a placement mistake into frame drops nobody can
  account for, and moving a `Buffer` the other way exhausts the internal heap
  the next `Hot` allocation needed. A failed allocation returns null, which
  every caller already has to handle, and says so in the log.
- Reflection ABI 12. `IMemoryProvider` gained `AllocateDma` at 11 and the raw
  allocation trio at 12; neither grew a struct, so the size fingerprint
  cannot see either, which is the case this number exists for. Two bumps in
  one unreleased cycle because 11 was on master for a few hours and package
  DLLs were built against it.

## 0.14.0

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

### Added
- `DEKI_TRANSFORM_3D`: `Deki::Object` gains `z`, rotation about X and Y, and
  `scaleZ`, with their world cache and accessors; `rotation` stays the Z angle.
  One composition serves both widths: when nothing in the chain rotates about
  X or Y it takes the 2D path bit for bit and only adds z, so 2D content
  composes at 2D cost and renders identically. The scene keys `z`,
  `rotationX`, `rotationY` and `scaleZ` are additive and written only when set,
  so a 2D project's files do not change; a 2D build skips them. The transform
  pseudo-component the state machine addresses gains the four fields. A package
  declares `transform_3d` to get the width.

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
