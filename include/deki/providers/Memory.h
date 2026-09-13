#pragma once

#include <stdint.h>
#include <stddef.h>
#include <memory>

// An allocation names itself by where it was written. Define
// DEKI_STRIP_ALLOC_CONTEXT to compile that away for a build that needs the
// flash back: the file paths, function names and line numbers all disappear
// and AllocContext becomes empty.
//
// Measured on the ESP32-S3 firmware, capture costs 5,664 bytes across roughly
// 118 call sites — 0.37% of a 1.5 MB image, about 48 bytes a site. That is
// cheap enough to be on by default and only worth stripping if you are
// genuinely short.
#if !defined(DEKI_STRIP_ALLOC_CONTEXT)
#define DEKI_ALLOC_CONTEXT_CAPTURE 1
#include <source_location>
#endif

// Forward declaration

namespace Deki
{

class IMemoryProvider;

namespace detail
{
// The raw allocation path, used by the global operator new on builds with
// exceptions off (src/providers/OperatorNew.cpp). Deliberately not on Memory
// itself: a component or a package reaching for these is a mistake — they
// carry no region, so the engine cannot place what they hand back, and
// they have no header, so they have no double-free guard. Use Deki::Buffer<T>
// or Memory::Allocate.
void* RawAllocate(size_t size);
void RawFree(void* ptr);
}  // namespace detail

/**
 * @brief Memory subsystem types - simplified (kept for compatibility)
 */
enum class MemorySubsystem : uint8_t
{
    GENERAL = 0  // Single subsystem - no tracking overhead
};

/**
 * @brief Who asked for an allocation. Filled in by the compiler.
 *
 * Nobody writes one. Every allocating call takes an AllocContext whose default
 * argument captures the call site, so an allocation is identified by where it
 * was written:
 *
 *     Deki::Memory::Allocate(n, Deki::Mem::Internal);
 *     // -> 'void ParticlePool::SetCapacity(size_t)' at ParticlePool.h:47
 *
 * There is no name to pass and no way to pass one. A hand-written tag is a
 * second identity for the same allocation that has to be kept in step with
 * the first, and the compiler already knows the answer.
 *
 * Nothing is copied or allocated. Every member points at a string literal the
 * compiler emitted with static storage duration, which is what makes it safe
 * for the debug leak table to hold the pointer.
 */
struct AllocContext
{
#if defined(DEKI_ALLOC_CONTEXT_CAPTURE)
    const char* file;      ///< __FILE__ of the call site
    const char* function;  ///< enclosing function, with its signature on GCC
    uint32_t line;

    // The default argument is the whole mechanism: it is evaluated at the
    // CALL SITE, so the allocation records where it was written rather than
    // anywhere in the engine it passes through. Every layer between — the
    // Buffer<T> methods, Memory::Allocate — takes one of these and forwards
    // it. A layer that took its own default instead would report itself, and
    // every allocation in the engine would name a plausible, wrong file.
    AllocContext(std::source_location loc = std::source_location::current())
        : file(loc.file_name()), function(loc.function_name()), line(loc.line())
    {
    }

    const char* File() const { return file; }
    const char* Name() const { return function; }
    uint32_t Line() const { return line; }
#else
    // Stripped: an empty struct, so nothing is captured, nothing is passed and
    // no string reaches the binary. The reporting calls still compile; they
    // just have nothing to say.
    AllocContext() = default;

    const char* File() const { return "?"; }
    const char* Name() const { return "?"; }
    uint32_t Line() const { return 0; }
#endif
};

// The layout differs between debug and release builds, so the engine and the
// packages that call into it have to agree — the same constraint MemoryStats
// already imposes, and the same one the reflection ABI number exists to
// police.


/// FNV-1a over a name, at compile time. The same function
/// AssetLookupTable::HashString computes at runtime, so a region name hashes
/// the way every other name in this engine does.
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

/**
 * @brief Where an allocation lives. The caller says; the engine never guesses.
 *
 * A region is a name, not an enumerator, so the set of them is open: a package
 * defines one and the engine needs no change to carry it.
 *
 *     namespace Deki::Mem
 *     {
 *     inline constexpr MemoryRegion Rtc = Region("rtc");
 *     }
 *
 * Deki::Mem is an ordinary namespace, so a package extends it the way it would
 * extend any other. What a package cannot do is SERVE a region: only the
 * board's IMemoryProvider can, and one that has not heard of "rtc" returns
 * nullptr and says so by name. Extension is two-sided on purpose — a package
 * names what it needs, a board decides whether it has it.
 *
 * The name is also the capability name, so a package that declares
 * platformRequires: ["rtc"] and one that writes Region("rtc") agree by
 * construction, and the build refuses the target before the allocation can
 * fail on it.
 *
 * The pointer is kept, never copied: Region() is only ever called on a string
 * literal, which has static storage duration.
 */
struct MemoryRegion
{
    uint32_t id;       ///< HashName(name); what the provider dispatches on
    const char* name;  ///< what a failure prints

    constexpr bool operator==(const MemoryRegion& other) const { return id == other.id; }
    constexpr bool operator!=(const MemoryRegion& other) const { return id != other.id; }
};

constexpr MemoryRegion Region(const char* name) { return MemoryRegion{HashName(name), name}; }

namespace Mem
{
/// Internal RAM. Fast, and the scarce one — a few hundred KB on an ESP32-S3.
inline constexpr MemoryRegion Internal = Region("internal");

/// External RAM (PSRAM) where the board has it. Megabytes, and markedly
/// slower: it is reached over SPI, so a buffer read every frame does not
/// belong here. Asking for it is a claim that this buffer's latency does not
/// matter.
inline constexpr MemoryRegion External = Region("external");
}  // namespace Mem

/**
 * @brief Memory statistics structure
 */
struct MemoryStats
{
    uint32_t totalAllocated;
    uint32_t psramAllocated;
    uint32_t internalRamAllocated;
#ifdef DEBUG
    uint32_t activeAllocations;
    uint32_t peakAllocations;
#endif
};

/**
 * @brief Memory Provider for Deki Engine
 *
 * Provides centralized memory management with PSRAM support.
 * Handles PSRAM usage and RAM allocation limits for the Deki.
 */
class Memory
{
    // The raw path needs the backend, which is private and should stay that
    // way. Friending the two functions is narrower than exposing a second
    // allocation API on Memory that components would find and misuse.
    friend void* detail::RawAllocate(size_t size);
    friend void detail::RawFree(void* ptr);

private:
    // Platform-specific memory provider
    static std::unique_ptr<IMemoryProvider> s_CurrentBackend;
    static bool initialized;
    static bool s_PsramAvailable;
    static bool s_PsramEnabled;

    // Global tracking
    static uint32_t totalAllocated;
    static uint32_t psramAllocated;
    static uint32_t internalRamAllocated;

#ifdef DEBUG
    // Lightweight leak detection (debug only)
    struct AllocationRecord
    {
        void* ptr;
        size_t size;
        const char* context;
        uint32_t line;      ///< 0 in a stripped build
        bool isPsram;
    };
    static AllocationRecord* s_AllocationRecords;
    static uint32_t s_MaxAllocations;
    // Free-slot stack + high-water mark: O(1) track/untrack (see .cpp).
    static uint32_t* s_FreeSlots;
    static uint32_t s_FreeSlotCount;
    static uint32_t s_NextFreshSlot;
    static bool s_TrackingFullReported;
    static uint32_t activeAllocations;
    static uint32_t peakAllocations;
    static uint32_t s_NextAllocationId;
#endif

public:
    /**
     * @brief Initialize memory backend
     * @return true if initialization successful
     */
    static bool Initialize();

    /**
     * @brief Shutdown memory backend
     */
    static void Shutdown();

    /**
     * @brief Set the memory provider implementation directly
     * @param backend Pointer to provider (takes ownership)
     */
    static void SetBackend(IMemoryProvider* backend);

    /**
     * @brief Check if the memory backend is initialized
     * @return true if initialized, false otherwise
     */
    static bool IsInitialized();

    /**
     * @brief Allocate memory, the engine choosing where it lives
     * @param size Size in bytes
     * @param region Where it lives. Resolved once, with no fallback to any
     *               other region; a region the board's provider does not serve
     *               returns nullptr and is named in the log
     * @param ctx Who is asking. Omit it — the compiler fills in the call site
     * @return Pointer to allocated memory, or nullptr if failed
     */
    static void* Allocate(size_t size,
                          MemoryRegion region,
                          AllocContext ctx = {});

    /**
     * @brief Allocate memory a DMA engine can read, from `region`
     *
     * Reachability is a constraint, not a place: a framebuffer must be
     * reachable whether the board keeps it in internal RAM or in PSRAM, and
     * saying so with a separate entry point means neither choice can silently
     * drop the requirement. On a platform where every allocation is equally
     * reachable this is the same as Allocate, and the provider says so.
     */
    static void* AllocateDma(size_t size,
                             MemoryRegion region,
                             AllocContext ctx = {});

    /**
     * @brief Deprecated: name the region instead of choosing PSRAM yourself
     *
     * true maps to Mem::External and false to Mem::Internal.
     */
    static void* Allocate(size_t size, bool prefer_psram, AllocContext ctx = {})
    {
        return Allocate(size, prefer_psram ? Mem::External : Mem::Internal, ctx);
    }

    // kLargeAllocation is gone with the size heuristic it served. Nothing
    // decides placement by size any more: a caller names Mem::External or
    // gets internal RAM.

    /**
     * @brief Allocate memory from internal RAM only (never external/PSRAM)
     * @param size Size in bytes to allocate
     * @param ctx Who is asking. Omit it — the compiler fills in the call site
     * @return Pointer to allocated memory, or nullptr if failed
     */
    static void* AllocateInternal(size_t size, AllocContext ctx = {});

    /**
     * @brief Free memory allocated via AllocateInternal
     * @param ptr Pointer to memory to free
     */
    static void FreeInternal(void* ptr);

    static void Free(void* ptr, AllocContext ctx = {});

    /**
     * @brief Allocate an array of `count` T, or return nullptr
     *
     * Use this instead of `new T[count]` for anything whose size comes from a
     * file, a scene, or the screen.
     *
     * `new[]` cannot fail gracefully on a device: ESP-IDF builds with
     * exceptions off, so the bad_alloc reaches __cxa_allocate_exception and
     * calls abort(), rebooting the board. `std::nothrow` does not help either,
     * because libstdc++ implements it by calling the throwing form inside a
     * try/catch that -fno-exceptions has removed. A caller that checks the
     * result can carry on without the thing it wanted, which is nearly always
     * better than a reboot.
     *
     * The multiplication is checked too. A count read from a file is attacker-
     * or corruption-controlled, and `count * sizeof(T)` is computed in a 32-bit
     * size_t on the device, so a large count silently wraps to a small one.
     *
     * Trivially copyable only: this does not construct anything, matching the
     * raw buffers it replaces.
     */
    template <typename T>
    static T* AllocateArray(size_t count, MemoryRegion region, AllocContext ctx = {})
    {
        if (count == 0)
            return nullptr;
        if (count > (SIZE_MAX / sizeof(T)))
            return nullptr;  // would wrap; no allocation could satisfy it anyway
        // ctx is forwarded, not defaulted again, so it still names the
        // caller of AllocateArray rather than this header.
        return static_cast<T*>(Allocate(count * sizeof(T), region, ctx));
    }

    /**
     * @brief Get current memory statistics
     * @return Memory stats structure
     */
    static MemoryStats GetStats();

    /**
     * @brief Get available PSRAM size
     * @return Available PSRAM size in bytes (0 if no PSRAM)
     */
    static size_t GetAvailablePSRAM();

    /**
     * @brief Get available internal RAM
     * @return Free internal RAM in bytes, or 0 where the platform does not
     *         report it (the desktop provider does not — the number is
     *         meaningless where the OS pages)
     *
     * The companion to GetAvailablePSRAM, which existed alone: the free
     * internal figure is the one a project is actually sized against, and it
     * was reachable only through a provider a caller does not have.
     */
    static size_t GetAvailableInternal();

    /**
     * @brief Bytes currently held by the raw (operator new) path
     * @return Live bytes in std::vector, std::string and scalar `new`
     *         allocations, or 0 when the build does not count them
     *
     * **Requires DEKI_MEMORY_TRACK_RAW**, and returns 0 without it. Counting
     * needs the block size on free, and with no header the only source is the
     * heap: measured at +35% per allocation (63-71 ns against 47-52 for
     * malloc+free), because on Windows that is _msize calling HeapSize and
     * taking a lock. Routing itself is free, so every build routes and only a
     * build that asks counts. Turn it on while hunting where memory went.
     *
     * Approximate even then: allocations made before the memory backend
     * exists (static initialisers) are freed through it afterwards, so the
     * total is clamped at zero rather than allowed to underflow into a
     * nonsense figure. It is a diagnostic, not an invariant.
     */
    static size_t GetRawAllocated();

    /**
     * @brief Log what is allocated and what is left, in one line
     * @param label Optional tag for the line ("after level load")
     *
     * For answering "how much memory am I using?" from game code without
     * assembling it from GetStats and a provider. Costs nothing unless
     * called; in debug builds it also reports the live and peak allocation
     * counts.
     */
    static void LogStats(const char* label = nullptr);

    /**
     * @brief Check if PSRAM is available
     * @return true if PSRAM available
     */
    static bool IsPSRAMAvailable();

private:
    // Both public entry points land here; needsDma is what separates them.
    static void* AllocateIn(size_t size, MemoryRegion region, bool needsDma, AllocContext ctx);

    /**
     * @brief Detect if PSRAM is available
     * @return true if PSRAM detected
     */
    static bool DetectPSRAM();

    /**
     * @brief Allocate from PSRAM specifically
     * @param size Size to allocate
     * @return Pointer or nullptr
     */
    static void* AllocatePSRAM(size_t size);

    /**
     * @brief Free PSRAM allocation
     * @param ptr Pointer to free
     */
    static void FreePSRAM(void* ptr);

    /**
     * @brief Update internal tracking counters
     * @param size Size of allocation (positive) or deallocation (negative)
     * @param isPsram Whether allocation used PSRAM
     * @param ptr Pointer for leak tracking (debug only)
     * @param context Context string for leak tracking (debug only)
     */
    static uint32_t UpdateTracking(int32_t size, bool isPsram, void* ptr = nullptr,
                                   const char* context = nullptr, uint32_t line = 0);

#ifdef DEBUG
    /**
     * @brief Track allocation for leak detection (debug only)
     */
    static uint32_t TrackAllocation(void* ptr, size_t size, const char* context,
                                    uint32_t line, bool isPsram);

    /**
     * @brief Untrack allocation for leak detection (debug only)
     */
    static void UntrackAllocation(void* ptr, uint32_t slot);

    /**
     * @brief Check for memory leaks (debug only)
     */
    static void CheckForLeaks();
#endif
};

}  // namespace Deki
