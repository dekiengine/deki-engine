# Changelog

Notable changes to the Deki engine core. The reflection ABI between the editor
and package DLLs has its own numbered history in
`include/deki/interop/ReflectionAbi.h`, summarised here.

## 0.15.0 - 2026-09-14

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
