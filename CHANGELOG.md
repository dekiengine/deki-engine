# Changelog

Notable changes to the Deki engine core. The reflection ABI between the editor
and package DLLs has its own numbered history in
`include/deki/interop/ReflectionAbi.h`, summarised here.

## Unreleased

### Added
- `DEKI_THREAD_LOCAL` (`deki/ThreadLocal.h`): `thread_local`, or a plain
  static in a build that defines `DEKI_NO_THREAD_LOCAL`. A Tactility device
  app is loaded by elf_loader, which has no thread-local storage, and its
  build defines it. The engine's own thread-locals (the log context, the
  instance-expansion guard) use it. Nothing changes for other builds.
- `Deki::kRGB565BigEndian` (`deki/Engine.h`): the byte order of RGB565 in
  memory, low byte first unless the board's platform `"defines"` has
  `DEKI_RGB565_BIG_ENDIAN`. Then the framebuffer and every image are high byte
  first, the order SPI panels take, so a frame can go to the panel as it is.
  A display whose panel takes the other order must swap. Nothing changes for
  a build without the define.
- `Texture2D::Load` swaps RGB565 and RGB565A8 pixels into that order.

### Fixed
- `FrameBufferBytes`' comment called RGB565A8 a 565 plane and an A8 plane; it
  is three bytes a pixel, the 565 value then alpha.

## 0.18.2 - 2026-10-11

### Added
- `Engine::GetLastFrameTimings()`: how long the last frame's update, render
  and present took, in milliseconds. The engine measured them already but
  only wrote them to the log.

## 0.18.1 - 2026-10-06

- No engine changes; released with editor 0.18.1, which fixes moving a
  project onto a new release's packages. Packages stay at 0.18.0.

## 0.18.0 - 2026-10-06

- **0.17 packages are refused.** `DEKI_OLDEST_SUPPORTED_PACKAGE` is 0.18.0:
  reflection ABI 21 breaks every package, so a project on 0.17 packages is
  told to move to 0.18.0 rather than failing to load them. Every package is
  released at 0.18.0 alongside this.

### Added
- **Screen Anchor** (`Deki::ScreenAnchor`) keeps its object at a point on the
  screen (a corner, the middle of an edge, or the centre) plus an offset in
  meters, on every screen size and while the camera moves. For a HUD. The
  render system places anchored objects before each frame through
  `ScreenAnchor::PlaceAll`; a scene file stores 0 for an anchored object's
  position.

### Changed
- `project_data.bin` is version 7 and must match the engine: the firmware and
  the data are exported together, so another version is refused with a
  message to export again. Older versions are not read.
- **Code style.** docs/codestyle holds the C++ style as a compiled example
  (CodeStyle.h, CodeStyle.cpp), with `.clang-format`, `.clang-tidy` and a
  pre-commit hook (tools/hooks). All engine code is formatted with
  clang-format 22 and named to the style.
- **Public names follow it.** Breaking for code using them:
  - `IFileSystem::OpenMode` values are `ReadBinary`, `WriteBinary`,
    `ReadText`, `WriteText`; `SeekOrigin` values are `Begin`, `Current`, `End`.
  - Constants take `k`: `kAssetTypeName`, `kInterfaceID`,
    `ProjectSettings::kMagic`, ... `InputKeys` codes are PascalCase (`Space`).
  - `MPType` values are PascalCase (`Nil`, `Int8`, `Str16`, ...).
  - The reflection attributes live in `Deki::DekiRefl` with PascalCase types
    (`Exported`, `Tooltip`, ...); the `DEKI_*` macros are unchanged.
  - The exports a package or project plugin provides are PascalCase:
    `DekiPluginRegisterComponents`, `DekiPluginGetReflectionAbi`,
    `DekiPluginInit`, ... A project's `src/PluginExports.cpp` written before
    this has to use the new names.
- **Reflection ABI 21.** Packages built before this are rebuilt.

### Removed
- `PropertyType::Asset`, the string GUID field type `AssetRef` replaced. Nothing
  used it. The `PropertyType` values after it moved down by one.
- `SceneMsgPackWriter::SetAssetResolver`, which only that type used.
- `Memory::Allocate(size, bool preferExternal)`. Name the region instead.
- Reading colours stored as packed integers in scenes. They are `#RRGGBBAA`.
- `Object::AddComponent(type, classHint)` and the scene's `"class"` key: a
  component is found by its type name, a bare class name, or a
  `DEKI_FORMER_NAME`. `MissingComponent::className` is gone.
- The `setup_components` alias of `PlatformSetupComponent::setupComponents`.
- The compiled scene keys nothing writes: `name`, `width`, `height`,
  `clear_color` and an object's `sort_order`.
- `SceneMsgPackWriter::SetSubAssetResolver`, which rewrote asset references
  that named a sprite frame in their `guid`.

### Added
- An enum value saved by name still loads after its enumerator is renamed to
  the style: a name that differs only in underscores and letter case is used
  when no name matches exactly (`Portrait_180` loads as `Portrait180`).

### Fixed
- The boot memory line no longer says "0 bytes free" on a host heap (editor,
  desktop simulator), which does not measure free space. It says so instead.

## 0.17.0 - 2026-09-30

### Added
- `IDisplay::GetColorFormat()`: the display's pixel format, which the engine
  gives the framebuffer along with its size (reflection ABI 19). Nothing
  compiles a format in: `DEKI_DEFAULT_COLOR_FORMAT` is gone.
- **The camera frames the world.** `<deki/ScreenScale.h>`'s
  `OrthoPixelsPerMeter(screenHeight, orthoHeight, pixelPerfect, art)` gives a
  screen's pixels per meter for a camera that shows `orthoHeight` meters top
  to bottom: a bigger screen of the same shape shows the same world, drawn
  bigger, and a wider one shows more at the sides. Pixel perfect rounds down to
  a whole multiple of the art density, never below 1x.
- `<deki/SceneMigration.h>`: `RegisterComponentsMigration` rewrites an object's
  saved components before they load (editor JSON only; device scenes are
  compiled from it). `ComponentsMigrationRegistrar` unregisters when a hot
  reload unloads the package.
- `Mat4::Perspective`.
- `AssetManager::ReadWholeFile(path, Buffer<uint8_t>&, Memory::Region)`: a
  whole file into a Buffer through the filesystem, for anything big a loader
  reads (a mesh): a std::vector would land on the internal heap and abort the
  board when it does not fit, where this logs the size and returns false.
- **A build's assets can live in internal storage.** `project_data.bin`
  (version 6) records where a build keeps its assets
  (`ProjectSettings::GetAssetStorage()`, header flag `FLAG_ASSETS_EXTERNAL`;
  older files read as external, where they always were).
  `AssetManager::LoadAssetRoot(root)` loads a build's `asset_table.bin`,
  `pack_index.bin` and assets from one folder, `Storage::AssetRoot()` names
  it: `F:/assets/` inside, `S:/` on a card. `Engine::Initialize` loads the
  internal one, and fails if a build that keeps its assets there has none;
  the package that mounts external storage loads that one.

### Changed
- **0.16 packages are refused.** `DEKI_OLDEST_SUPPORTED_PACKAGE` is 0.17.0:
  reflection ABI 20 and `ICamera`'s framing (`GetPixelsPerMeter(w, h)`,
  `SetFixedPixelsPerMeter`) break every package, so a project on 0.16.0
  packages is told to move to 0.17.0 rather than failing to compile
  `deki-rendering`. Every package is released at 0.17.x alongside this.
- **The framebuffer is the display's size.** `Engine::SetDisplay` sizes it from
  `IDisplay::GetDisplaySize()`; the engine no longer reads `DEKI_SCREEN_WIDTH`/
  `HEIGHT` and needs only `DEKI_DEFAULT_COLOR_FORMAT` from the target. With no
  display, or one that reports no size, nothing is rendered and the log says
  so; a failed allocation names the size and the bytes it needed.
- **`ICamera`** (reflection ABI 18, then 20): `GetPixelsPerMeter(bufferWidth,
  bufferHeight)` is resolved per buffer size; `SetPixelsPerMeter` became
  `GetOrthoHeight`/`SetOrthoHeight` plus `SetFixedPixelsPerMeter` for views
  that are not a screen; the projection, field of view and clip planes joined
  it, and `GetProjectionMatrix` builds either kind.
- The Framebuffer settings section (`DisplayProjectSettings`) is gone: nothing
  about the screen is a project setting any more. `ProjectSettings::Write` no
  longer takes a display size, and the two `DisplaySettings` size fields in
  `project_data.bin` are reserved (the layout is unchanged).
- **`dproject.bin` is `project_data.bin`**, named like the other exported
  files (`asset_table.bin`, `pack_index.bin`). `ProjectSettings::Write` takes
  the asset storage first.
- `Storage::Flash` and `Storage::SDCard` are `Storage::Internal` and
  `Storage::External` (same values).
- A pack index names each pack by file name; the runtime finds it next to the
  index, on whichever storage the assets are. It held `S:/<guid>.pack`, which
  is on the card and, with `F:/assets/`, would not fit its 48 bytes.

### Fixed
- **`SetDisplay` sizes the framebuffer whether or not `Initialize` has run.**
  It waited for `Initialize`, which the editor never calls, so play mode
  kept the framebuffer at its old size and refused to start on any other
  screen ("Play mode could not get a 320x240 framebuffer").
- **A package whose directory name begins with another package's failed the
  firmware build** ("source path matches two package tags"). The reflection
  code generator matched a source file to its package by substring, so a
  file in `deki-input-embedded/` also matched `deki-input`. A tag now has to
  be a whole path component.

### Changed
- The reflection code generator wraps its field tables in a scoped
  `-Winvalid-offsetof` suppression. Components have a vtable, so `offsetof`
  on them is "conditionally supported": GCC gets it right and only warns, but
  ESP-IDF 6.1 turns the warning into an error.
- **The engine knows no target by name.** `CMakeLists.txt` had an
  `if(DEFINED SIMULATOR)` block that compiled the engine for 320x240 RGB565 and
  forced `DEKI_PACKAGE_SDL3`, `DEKI_PACKAGE_2D` and `DEKI_PACKAGE_TWEEN` on as
  public defines. A game build sets nothing now: whoever adds the engine's
  directory - the target's build backend - passes `DEKI_SCREEN_WIDTH`,
  `DEKI_SCREEN_HEIGHT`, `DEKI_DEFAULT_COLOR_FORMAT` and the rest through
  `target_compile_definitions(deki-engine-core PUBLIC ...)`, and `Engine.cpp`
  refuses to compile without them. No engine source ever tested a package
  define, so the editor build's two are gone as well.
- **Memory speaks of the External region, not of PSRAM.** PSRAM is what one kind
  of target calls it. `MemoryStats::psramAllocated` -> `externalAllocated`,
  `Memory::GetAvailablePSRAM()` -> `GetAvailableExternal()`,
  `Memory::IsPSRAMAvailable()` -> `IsExternalAvailable()`; the deprecated
  `Allocate(size, bool)` overload's flag is `preferExternal` (callers passing it
  positionally are unaffected). Layouts are unchanged, so the reflection ABI is
  too.

### Fixed
- `Scene::DuplicateObject` gave the copy, and every object under it, its
  original's id: it clones through the scene JSON, which carries ids. A scene
  built by duplicating (the editor's Duplicate, or the CLI's
  `duplicate_object`) saved many objects under one id. The copy now gets
  fresh ids, and its references to objects inside it move to the copies, as
  spawning a prefab already did. **A scene saved with shared ids keeps them**:
  rebuild it, or re-duplicate the affected objects.
- With logging compiled out, the `DEKI_LOG_*` macros expanded to nothing, so
  `if (x) DEKI_LOG_INFO(...);` became an empty `if` body, an error under
  ESP-IDF 6. They expand to `((void)0)` now; arguments are still not
  evaluated.
- The simulator's engine assumed 320x240 RGB565 regardless of its platform, so
  a simulator platform of any other size set the renderer up wrong.
- A define of a package stripped from a simulator build came back in through
  the engine's public defines.

### Removed
- `Memory::DetectPSRAM`, `AllocatePSRAM`, `FreePSRAM`: private, and never called.

## 0.16.0 - 2026-09-16

### Fixed
- A component's former names are now indexed by hash as well as by string, so
  a reference that stores only a type id resolves across a rename. `ObjectRef`
  and `PropertyRef` both store identity without a name beside it, and a hash
  cannot be mapped back to the string it came from, so the `DEKI_FORMER_NAME`
  fallback could not reach them: every such reference to a package component
  went dead when 0.16.0 moved those components into namespaces, and the stale
  value was written back on save. Both are now rewritten to the current
  identity as they load, so a file repairs itself the first time it is saved.
- The generated field table asserts that a component fits in 65535 bytes.
  `FieldRef` stores a 16-bit offset to keep the device's table at 8 bytes per
  field, and the cast that produced it was silent, so a larger component would
  have had its later fields point at arbitrary memory.

Reflection ABI 17. Every package must be rebuilt.

### Changed
- **Component identity is a qualified name in practice, not just in theory.**
  Packages moved their types into a namespace of their own, so nothing of a
  package's sits at global scope. `IRenderSystem` names
  `DekiRendering::DekiRenderer` and the node metadata in `Property.h` is
  `DekiNodeGraph::DekiNodeMeta`: both belong to packages, and the engine only
  stores and returns them. No layout moved, but the mangling of anything naming
  those types did, hence the ABI bump.
- **`SceneFormat::SceneMsgPackParser` is forward declared in `Property.h`.** The
  generated headers that name it are included INSIDE a package's namespace,
  beside the type they belong to, so the declaration they used to carry made
  `Pkg::Deki::SceneFormat` - and that inner `Deki` then hid the engine's
  namespace from every lookup in the rest of the package.
- **`PropertyInfo` carries `enumInts`**, the underlying value of each
  enumerator beside the existing names, so a name can be resolved back to the
  number the field holds. Nothing could before, which is why authored files
  stored a bare integer. Trails the struct under the append-only rule, so
  `sizeofPropertyInfo` refuses a package built before it.
- **`ComponentMeta` carries `formerNames`** for `DEKI_FORMER_NAME`, and the
  registry resolves an unrecognised type name through them.

### Added
- **A package's automatic SetupComponent declares when it runs.**
  `DEKI_REGISTER_EDITOR_AUTO_SETUP` fires when packages finish loading, which is
  every project open and every hot reload, and suits something cheap and local.
  The new `DEKI_REGISTER_PLAY_AUTO_SETUP` waits for the user to press Play, for
  anything with a side effect beyond the editor. There was only one moment
  before, so a setup that reached the network ran every time a project was
  opened. Registering without a phase still means editor load.
- `DEKI_OLDEST_SUPPORTED_PACKAGE`: the oldest package generation this engine
  can build. Releases are in lockstep, so a package's `minEngine` is also the
  engine it was built against, and the editor refuses anything older rather
  than letting it fail to compile later. Bump it on any release that breaks
  packages.

### Fixed
- The codegen's precompiled headers: one per flag set, but only the first ever
  got a rebuild rule, so the rest went stale whenever an engine reflection
  header changed.


## 0.15.0 - 2026-09-14

### Added
- `Deki::AllocContext`, so an allocation names itself. The `context` argument
  is no longer written by hand: its default argument captures the call site
  via `std::source_location`, and a failure reports the file, line and
  function that asked. It names the CALLER - `Buffer<T>::Allocate` and
  `Memory::Allocate` forward the context rather than re-defaulting it, so the
  location is where you wrote the call and not `Buffer.h`, which a test pins
  because getting it wrong would report a plausible file for every allocation
  in the engine. A literal still wins where the call site is not the useful
  name, and stays implicit so existing call sites compile unchanged.

  **Debug builds only.** Capturing embeds file paths and function names in
  the binary forever, which is a poor trade on a part where code size is a
  budget, so it compiles out unless `DEBUG` is defined (or `DEKI_NO_ALLOC_CONTEXT`
  switches it off in a debug build). In release `AllocContext` is a single
  pointer - exactly what the old `const char*` parameter was - and the
  measured firmware cost is +32 bytes, 0.002%. On device the failure line
  says `build with DEBUG for the call site` instead of inventing an origin it
  does not have. Available at all because the engine moved to C++23 this
  cycle; `std::source_location` compiles on the xtensa toolchain at
  `gnu++2b`.
- `Deki::Vector<T>`: a growable array whose growth can fail. `std::vector`
  cannot - `push_back` calls `operator new`, which with exceptions off has no
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
  46-51 ns/op against 47-52 for malloc+free - free, within noise. Counting
  the bytes is separate and opt-in behind `DEKI_MEMORY_TRACK_RAW` (63-71
  ns/op, +35%), because with no header the block size has to come from the
  heap and on Windows that is `_msize` taking a lock.
- Global `operator new`/`delete` on builds with exceptions off
  (`src/providers/OperatorNew.cpp`), which is the half `Deki::Buffer` cannot
  reach: `std::vector` and `std::string` call `operator new` internally, so
  their growth failures were the last silent reboot. A failure now says how
  many bytes it wanted before it aborts, and `std::nothrow` works -
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
  actually had was an ownership bug rather than a churn bug - a `delete[]`
  paired with a `Deki::Memory` allocation, a buffer nothing freed, a cleanup
  path that released something a borrowing function did not own, a null pointer
  that reached a `memcpy` because one error branch forgot to return - and a
  destructor cannot make any of those mistakes. Move-only; `T` must be
  trivially copyable and trivially destructible, both enforced by
  `static_assert` because nothing here constructs or destroys elements.
- `Deki::Memory::Region`: where an allocation lives, named rather than
  enumerated. A region is a `constexpr` value built from a string, carrying
  that string and its FNV-1a hash: `Memory::Internal` and `Memory::External`
  are declared by the engine, and a package declares its own the same way, in
  its own namespace - `inline constexpr Deki::Memory::Region kRtc{"esp32.rtc"};`
  - without the engine knowing the name. Comparison is an integer compare, so
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
  gracefully on the device - ESP-IDF builds with exceptions off, so the
  `bad_alloc` reaches `__cxa_allocate_exception` and aborts, rebooting the
  board, and `std::nothrow` aborts identically because libstdc++ implements it
  by calling the throwing form inside a `try`/`catch` that `-fno-exceptions`
  has removed. The multiplication is checked too: `count * sizeof(T)` is a
  32-bit `size_t` on the device, so a corrupt count silently wraps small.
- `Memory::AllocateDma` and `IMemoryProvider`'s `needsDma` flag, with
  `MALLOC_CAP_DMA` behind it on the ESP32. DMA is a property of an
  allocation, not a place to put one - a buffer can need to be
  DMA-reachable in either region - so it is a separate call rather than a
  region of its own. The old `MemoryUse::Dma` had documented a promise the
  allocator was not keeping.
- The boot path reports what it cost. `Memory::LogStats` is called once when
  the boot scene is up, and again when a setup scene has loaded the startup
  scene, so a device log says how many bytes are in each region before the
  game loop allocates anything. A platform that declares no boot scene of its
  own gets the game scene baked in as one, so on those the first call is the
  only report there will be; where both happen the labels tell them apart.
  The function existed and had no callers, which left the one number worth
  having on a device - how much external RAM the boot path takes - written
  down nowhere. An S3 with 4MB PSRAM under QEMU now logs `Memory at boot
  scene: 153600 bytes allocated (0 internal, 153600 external); 121335 free
  internal, 4037684 free external`.

### Changed
- **`MemoryUse` is replaced by `Memory::Region` - required, never inferred.**
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
  a graphics engine - the largest allocations are usually the ones touched
  every frame, and a framebuffer in PSRAM measures markedly worse than one in
  internal RAM on real ESP32-S3 hardware. `Hot` and `Buffer` named what memory
  was *for* rather than where it goes, and `Hot` placed identically to
  `Internal`, so it was a label pretending to be a behaviour.
  `kLargeAllocation` went with the rule it served, and `Deki::Vector` takes
  its region as a constructor argument with no default constructor. Every call
  site in the engine and all 25 packages already passed a placement
  explicitly, so the change was mechanical.
- **`IMemoryProvider`'s placement virtuals are pure.** `AllocateInternal`,
  `AllocateDMA`, `FreeInternal` and `GetAvailableInternalRAM` had defaults -
  `malloc`, and `AllocateDMA` forwarding to `AllocateInternal`. That reads as
  a convenience for hosts and is a bug generator on a board: a provider for a
  new part that forgot `AllocateDMA` would inherit the forward and hand a
  display peripheral memory it cannot reach, silently. Every provider now
  states what each region means on its platform, even where the answer is
  "the same as the others here". Making a virtual pure moves no vtable slot,
  so this costs no ABI version on its own - it just stops a provider
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
  builds were pinned at 17 - the same package source built at two standards
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
