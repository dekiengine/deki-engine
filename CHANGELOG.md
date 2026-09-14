# Changelog

Notable changes to the Deki engine core. The reflection ABI between the editor
and package DLLs has its own numbered history in
`include/deki/interop/ReflectionAbi.h`, summarised here.

## Unreleased

### Added
- `Deki::AllocContext`, so an allocation names itself. The `context` argument
  is no longer written by hand: its default argument captures the call site
  via `std::source_location`, and a failure reports the file, line and
  function that asked. It names the CALLER — `Buffer<T>::Allocate` and
  `Memory::Allocate` forward the context rather than re-defaulting it, so the
  location is where you wrote the call and not `Buffer.h`, which a test pins
  because getting it wrong would report a plausible file for every allocation
  in the engine. A literal still wins where the call site is not the useful
  name, and stays implicit so existing call sites compile unchanged.

  **Debug builds only.** Capturing embeds file paths and function names in
  the binary forever, which is a poor trade on a part where code size is a
  budget, so it compiles out unless `DEBUG` is defined (or `DEKI_NO_ALLOC_CONTEXT`
  switches it off in a debug build). In release `AllocContext` is a single
  pointer — exactly what the old `const char*` parameter was — and the
  measured firmware cost is +32 bytes, 0.002%. On device the failure line
  says `build with DEBUG for the call site` instead of inventing an origin it
  does not have. Available at all because the engine moved to C++23 this
  cycle; `std::source_location` compiles on the xtensa toolchain at
  `gnu++2b`.
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
- `Memory::Initialize` logs the free internal and external RAM once, so a
  project can be sized against real numbers instead of against its first
  out-of-memory line.
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
- `Deki::Memory::Region`: where an allocation lives, named rather than
  enumerated. A region is a `constexpr` value built from a string, carrying
  that string and its FNV-1a hash: `Memory::Internal` and `Memory::External`
  are declared by the engine, and a package declares its own the same way, in
  its own namespace — `inline constexpr Deki::Memory::Region kRtc{"esp32.rtc"};`
  — without the engine knowing the name. Comparison is an integer compare, so
  this costs no more than the enum it replaces, and the name survives into the
  failure message. Two packages whose names hash the same are reported at
  startup with both names.

  Extension is two-sided: a package can name a region, but only the board's
  `IMemoryProvider` can serve one, and a provider that has not heard of
  `esp32.rtc` returns null and says so by name. The region name is also the
  capability name with a `memory:` prefix, so a package allocating from
  `kRtc` declares `"platformRequires": ["memory:esp32.rtc"]` and a target
  without it is refused before anything runs.

  Required on every allocating call: omitting the region does not compile.
- `Deki::Memory::AllocateArray<T>`: a checked replacement for `new T[n]` for
  any count that comes from a file, a scene, or the screen. `new[]` cannot fail
  gracefully on the device — ESP-IDF builds with exceptions off, so the
  `bad_alloc` reaches `__cxa_allocate_exception` and aborts, rebooting the
  board, and `std::nothrow` aborts identically because libstdc++ implements it
  by calling the throwing form inside a `try`/`catch` that `-fno-exceptions`
  has removed. The multiplication is checked too: `count * sizeof(T)` is a
  32-bit `size_t` on the device, so a corrupt count silently wraps small.
- `Memory::AllocateDma` and `IMemoryProvider`'s `needsDma` flag, with
  `MALLOC_CAP_DMA` behind it on the ESP32. DMA is a property of an
  allocation, not a place to put one — a buffer can need to be
  DMA-reachable in either region — so it is a separate call rather than a
  region of its own. The old `MemoryUse::Dma` had documented a promise the
  allocator was not keeping.
- The boot path reports what it cost. `Memory::LogStats` is called once when
  the boot scene is up, and again when a setup scene has loaded the startup
  scene, so a device log says how many bytes are in each region before the
  game loop allocates anything. A platform that declares no boot scene of its
  own gets the game scene baked in as one, so on those the first call is the
  only report there will be; where both happen the labels tell them apart.
  The function existed and had no callers, which left the one number worth
  having on a device — how much external RAM the boot path takes — written
  down nowhere. An S3 with 4MB PSRAM under QEMU now logs `Memory at boot
  scene: 153600 bytes allocated (0 internal, 153600 external); 121335 free
  internal, 4037684 free external`.

### Changed
- **`MemoryUse` is replaced by `Memory::Region` — required, never inferred.**
  Placement is not defaulted and not guessed: every allocating call names its
  region and omitting it does not compile.

  *Migration.* `MemoryUse::Internal` and `MemoryUse::Hot` become
  `Deki::Memory::Internal`; `MemoryUse::Buffer` becomes
  `Deki::Memory::External`; `MemoryUse::Dma` becomes `Memory::AllocateDma`
  (or `Buffer::AllocateDma`) with the region you actually want, since
  DMA-reachable and external are independent questions. The
  `bool prefer_psram` overloads are gone.

  The enum was closed, so only the engine could add a region and a board with
  RTC-backed or on-chip-SRAM memory had nowhere to put it. A region is now an
  open name that a package declares for itself. `Auto` is gone with the enum:
  it sent anything at or above 16 KB to PSRAM, which has the sign backwards in
  a graphics engine — the largest allocations are usually the ones touched
  every frame, and a framebuffer in PSRAM measures markedly worse than one in
  internal RAM on real ESP32-S3 hardware. `Hot` and `Buffer` named what memory
  was *for* rather than where it goes, and `Hot` placed identically to
  `Internal`, so it was a label pretending to be a behaviour.
  `kLargeAllocation` went with the rule it served, and `Deki::Vector` takes
  its region as a constructor argument with no default constructor. Every call
  site in the engine and all 25 packages already passed a placement
  explicitly, so the change was mechanical.
- **`IMemoryProvider`'s placement virtuals are pure.** `AllocateInternal`,
  `AllocateDMA`, `FreeInternal` and `GetAvailableInternalRAM` had defaults —
  `malloc`, and `AllocateDMA` forwarding to `AllocateInternal`. That reads as
  a convenience for hosts and is a bug generator on a board: a provider for a
  new part that forgot `AllocateDMA` would inherit the forward and hand a
  display peripheral memory it cannot reach, silently. Every provider now
  states what each region means on its platform, even where the answer is
  "the same as the others here". Making a virtual pure moves no vtable slot,
  so this costs no ABI version on its own — it just stops a provider
  compiling until it has decided. It immediately found three test providers
  and the desktop provider relying on the forward.
- The out-of-memory line dropped `; no fallback is attempted`. It restated
  the `Failed` the line opens with, and the placement rule it was really
  explaining is already printed once at startup.
- `Memory::Shutdown` clears the external-RAM flags. They outlived the backend
  that reported them, so after a shutdown the engine still believed PSRAM was
  present and a later `Memory::External` allocation failed with "Failed to
  allocate N bytes in 'external'" on a machine with gigabytes free.
- C++23 (`CMAKE_CXX_STANDARD 23`) instead of 17. ESP-IDF forces
  `-std=gnu++2b` for chip targets unconditionally, so firmware had been
  compiling at C++23 while the engine, the editor and the generated project
  builds were pinned at 17 — the same package source built at two standards
  depending on target, so a C++20 feature would compile for the board and
  fail on the desktop. Two identifiers named `requires` had to be renamed
  (it is a keyword since C++20) and nothing else broke.
- A log line's colour reset sits before its newline, not after. It used to be
  written after, so the escape landed at the start of whatever was printed
  next: invisible on a terminal, but anywhere stdout and stderr are combined
  into one stream it prefixed the following line with `[0m`, which is
  enough to stop a parser anchored at the start of a line from matching it.

- Reflection codegen keeps one precompiled header per flag set instead of one
  for the whole build tree. A `.gch` records the defines it was built under
  and GCC refuses one whose defines do not match, so a single shared directory
  served whichever caller reached it first and every other caller parsed the
  headers from scratch with a `-Winvalid-pch` warning. It went unnoticed while
  the editor was the only caller; the per-package compile checks have 23
  different define sets. Callers that agree on flags still share one PCH.
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
