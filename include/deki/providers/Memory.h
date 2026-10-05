#pragma once

#include <stdint.h>
#include <stddef.h>
#include <memory>

// An allocation names itself by where it was written. Define
// DEKI_STRIP_ALLOC_CONTEXT to compile that away when flash is short: the file
// paths, function names and line numbers disappear and AllocContext is empty.
// On the ESP32-S3 firmware capture costs about 48 bytes per call site (5.6 KB,
// 0.37% of a 1.5 MB image), so it is on by default.
#if !defined(DEKI_STRIP_ALLOC_CONTEXT)
#define DEKI_ALLOC_CONTEXT_CAPTURE 1
#include <source_location>
#endif

namespace Deki
{

class IMemoryProvider;

namespace Detail
{
// The raw allocation path, used by the global operator new on builds with
// exceptions off (src/providers/OperatorNew.cpp). Not on Memory, because
// components and packages must not use it: it has no region, so the engine
// cannot place the memory, and no header, so no double-free guard. Use
// Deki::Buffer<T> or Memory::Allocate.
void* RawAllocate(size_t size);
void RawFree(void* ptr);
}  // namespace Detail

/// Who asked for an allocation, filled in by the compiler.
///
/// Nobody writes one. Every allocating call takes an AllocContext whose
/// default argument captures the call site, so an allocation is identified by
/// where it was written:
///
///     Deki::Memory::Allocate(n, Deki::Memory::Internal);
///     // -> 'void ParticlePool::SetCapacity(size_t)' at ParticlePool.h:47
///
/// Nothing is copied or allocated: every member points at a string literal
/// with static storage, so the debug leak table can keep the pointer.
struct AllocContext
{
#if defined(DEKI_ALLOC_CONTEXT_CAPTURE)
    const char* file;      ///< __FILE__ of the call site
    const char* function;  ///< enclosing function, with its signature on GCC
    uint32_t line;

    // The default argument is evaluated at the call site, so the allocation
    // records where it was written. Every layer in between (the Buffer<T>
    // methods, Memory::Allocate) must take one and pass it on; a layer that
    // used its own default would report itself instead of the caller.
    AllocContext(std::source_location loc = std::source_location::current())
        : file(loc.file_name()),
          function(loc.function_name()),
          line(loc.line())
    {
    }

    const char* File() const { return file; }
    const char* Name() const { return function; }
    uint32_t Line() const { return line; }
#else
    // Stripped: an empty struct, so nothing is captured and no string reaches
    // the binary. The reporting calls still compile and return placeholders.
    AllocContext() = default;

    const char* File() const { return "?"; }
    const char* Name() const { return "?"; }
    uint32_t Line() const { return 0; }
#endif
};

// The layout differs between debug and release builds, so the engine and its
// packages must be built the same way (as MemoryStats already requires).

/// FNV-1a over a name, at compile time: the same hash as
/// AssetLookupTable::HashString, so region names hash like every other name.
constexpr uint32_t HashName(const char* s)
{
    uint32_t h = 2166136261u;
    while (*s)
    {
        h ^= static_cast<uint8_t>(*s++);
        h *= 16777619u;
    }
    return h;
}

struct MemoryStats
{
    uint32_t totalAllocated;
    uint32_t externalAllocated;
    uint32_t internalRamAllocated;
#ifdef DEBUG
    uint32_t activeAllocations;
    uint32_t peakAllocations;
#endif
};

/// The engine's memory, in named regions. Internal is the heap every target
/// has; External is a second, larger, slower memory a target may offer (such
/// as PSRAM on a microcontroller). What a region maps to on a target is up to
/// that target's provider.
class Memory
{
    // The raw path needs the private backend. Friending these two functions
    // keeps a second allocation API off Memory, where components would use it.
    friend void* Detail::RawAllocate(size_t size);
    friend void Detail::RawFree(void* ptr);

private:
    static std::unique_ptr<IMemoryProvider> s_CurrentBackend;
    static bool s_Initialized;
    static bool s_ExternalAvailable;
    static bool s_ExternalEnabled;

    static uint32_t s_TotalAllocated;
    static uint32_t s_ExternalAllocated;
    static uint32_t s_InternalRamAllocated;

#ifdef DEBUG
    // Leak detection (debug only).
    struct AllocationRecord
    {
        void* ptr;
        size_t size;
        const char* context;
        uint32_t line;  ///< 0 in a stripped build
        bool isExternal;
    };
    static AllocationRecord* s_AllocationRecords;
    static uint32_t s_MaxAllocations;
    // Free-slot stack and high-water mark, so tracking is O(1) (see .cpp).
    static uint32_t* s_FreeSlots;
    static uint32_t s_FreeSlotCount;
    static uint32_t s_NextFreshSlot;
    static bool s_TrackingFullReported;
    static uint32_t activeAllocations;
    static uint32_t peakAllocations;
    static uint32_t s_NextAllocationId;
#endif

public:
    /// Where an allocation lives. The caller says; the engine never guesses.
    ///
    /// A region is a name, not an enumerator, so packages can define new ones
    /// without an engine change, in their own namespace:
    ///
    ///     namespace DekiEsp32
    ///     {
    ///     inline constexpr Deki::Memory::Region Rtc{"esp32.rtc"};
    ///     }
    ///
    /// A region's identity is its hashed name, so the namespace it is declared
    /// in does not matter, and two packages' "Rtc" constants cannot collide.
    ///
    /// Only the board's IMemoryProvider can serve a region; one that does not
    /// know "esp32.rtc" returns nullptr and names it. A package that allocates
    /// from Region("esp32.rtc") declares platformRequires ["memory:esp32.rtc"],
    /// so the build refuses a target without it before anything runs.
    ///
    /// The name pointer is kept, not copied: a Region is only built from a
    /// string literal, which has static storage.
    struct Region
    {
        uint32_t id;       ///< HashName(name); what the provider dispatches on
        const char* name;  ///< what a failure prints

        constexpr explicit Region(const char* regionName)
            : id(HashName(regionName)),
              name(regionName)
        {
        }

        constexpr bool operator==(const Region& other) const { return id == other.id; }
        constexpr bool operator!=(const Region& other) const { return id != other.id; }

    private:
        /// Rebuilds a region from the id in an allocation header, which holds
        /// no name. Private to Memory: a public form would let anyone make a
        /// Region whose id and name disagree.
        constexpr Region(uint32_t storedId, const char* placeholderName)
            : id(storedId),
              name(placeholderName)
        {
        }

        friend class Memory;
    };

    // The two regions are named values, used like enum values.
    // NOLINTBEGIN(readability-identifier-naming)

    /// Internal RAM: fast and scarce, a few hundred KB on an ESP32-S3.
    static const Region Internal;

    /// External RAM (PSRAM) where the board has it: megabytes, but much slower
    /// because it is reached over SPI. Use it only for buffers whose latency
    /// does not matter, not for one read every frame.
    static const Region External;
    // NOLINTEND(readability-identifier-naming)

    /// Returns false on failure.
    static bool Initialize();

    static void Shutdown();

    /// Installs the memory provider and takes ownership of it.
    static void SetBackend(IMemoryProvider* backend);

    static bool IsInitialized();

    /// Allocates `size` bytes in `region`, or returns nullptr. There is no
    /// fallback to another region; a region the board's provider does not
    /// serve returns nullptr and is named in the log. Leave `ctx` out: the
    /// compiler fills in the call site.
    static void* Allocate(size_t size, Memory::Region region, AllocContext ctx = {});

    /// Allocates memory a DMA engine can read, in `region`. DMA reach is a
    /// requirement on top of the region (a framebuffer may live in internal
    /// RAM or PSRAM), and a separate entry point keeps it from being dropped.
    /// Where all memory is DMA-reachable this is the same as Allocate.
    static void* AllocateDma(size_t size, Memory::Region region, AllocContext ctx = {});

    // Placement is never decided by size: a caller names
    // Deki::Memory::External or gets internal RAM.

    /// Allocates `size` bytes in internal RAM only, never PSRAM, or returns
    /// nullptr. Leave `ctx` out: the compiler fills in the call site.
    static void* AllocateInternal(size_t size, AllocContext ctx = {});

    /// Frees memory from AllocateInternal.
    static void FreeInternal(void* ptr);

    static void Free(void* ptr, AllocContext ctx = {});

    /// Allocates `count` T, or returns nullptr. Use it instead of
    /// `new T[count]` for anything sized by a file, a scene or the screen.
    ///
    /// `new[]` cannot fail gracefully on a device: ESP-IDF builds with
    /// exceptions off, so a failed allocation calls abort() and reboots the
    /// board, and `std::nothrow` does not help because libstdc++ implements
    /// it with the throwing form. A caller that checks for nullptr can carry
    /// on instead.
    ///
    /// The size multiplication is checked: a count from a corrupt file and a
    /// 32-bit size_t would otherwise wrap to a small size.
    ///
    /// For trivially copyable T only: nothing is constructed.
    template <typename T>
    static T* AllocateArray(size_t count, Memory::Region region, AllocContext ctx = {})
    {
        if (count == 0)
        {
            return nullptr;
        }
        if (count > (SIZE_MAX / sizeof(T)))
        {
            return nullptr;  // would wrap; no allocation could satisfy it anyway
        }
        // Pass ctx on, never a new default, so it names the caller of
        // AllocateArray rather than this header.
        return static_cast<T*>(Allocate(count * sizeof(T), region, ctx));
    }

    static MemoryStats GetStats();

    /// Free bytes in the External region, 0 when the target has none.
    static size_t GetAvailableExternal();

    /// Free internal RAM in bytes, the figure a project is sized against. 0
    /// where the platform does not report it (the desktop provider, since the
    /// OS pages).
    static size_t GetAvailableInternal();

    /// Live bytes on the raw (operator new) path: std::vector, std::string and
    /// scalar `new`. Requires DEKI_MEMORY_TRACK_RAW and returns 0 without it.
    /// Counting needs the block size on free, which only the heap knows, and
    /// asking costs about 35% per allocation (on Windows, _msize takes a
    /// lock), so only a build that asks counts. Turn it on while hunting where
    /// memory went.
    ///
    /// Approximate even then: allocations from static initialisers, before
    /// the backend exists, are freed through it later, so the total is clamped
    /// at zero. A diagnostic, not an invariant.
    static size_t GetRawAllocated();

    /// Logs what is allocated and what is left, in one line, with an optional
    /// `label` (such as "after level load"). Debug builds add the live and
    /// peak allocation counts.
    static void LogStats(const char* label = nullptr);

    /// True when the target's provider serves the External region.
    static bool IsExternalAvailable();

private:
    // Both public entry points land here; needsDma tells them apart.
    static void* AllocateIn(size_t size, Memory::Region region, bool needsDma, AllocContext ctx);

    /// Adds `size` (negative on free) to the counters. `ptr`, `context` and
    /// `line` feed leak tracking in debug builds.
    static uint32_t UpdateTracking(int32_t size, bool isExternal, void* ptr = nullptr, const char* context = nullptr,
                                   uint32_t line = 0);

#ifdef DEBUG
    static uint32_t TrackAllocation(void* ptr, size_t size, const char* context, uint32_t line, bool isExternal);

    static void UntrackAllocation(void* ptr, uint32_t slot);

    /// Reports allocations still live.
    static void CheckForLeaks();
#endif
};

// Defined after the class, where Memory is a complete type. inline gives one
// definition across translation units; constexpr goes on the definition, not
// the declaration ([dcl.constexpr]).
// NOLINTBEGIN(readability-identifier-naming)
inline constexpr Memory::Region Memory::Internal{ "internal" };
inline constexpr Memory::Region Memory::External{ "external" };
// NOLINTEND(readability-identifier-naming)

}  // namespace Deki
