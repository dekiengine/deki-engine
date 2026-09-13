#pragma once

#include <stdint.h>
#include <stddef.h>
#include <memory>

// Capturing the call site costs flash: every site that omits a label embeds
// its file path, its function name and a line number, and those strings live
// in the binary forever. That is a fine trade in the editor and a poor one on
// a part where code size is a budget, so it is a debug-build feature.
//
// Define DEKI_NO_ALLOC_CONTEXT to switch it off in a debug build too.
#if defined(DEBUG) && !defined(DEKI_NO_ALLOC_CONTEXT)
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
// carry no MemoryUse, so the engine cannot place what they hand back, and
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
 * Every allocating call takes one of these and almost nobody writes one: the
 * default argument captures the call site, so an allocation names itself
 * without the caller typing a label that can go stale after a rename or get
 * copied verbatim into the next function along.
 *
 *     Deki::Memory::Allocate(n, Deki::MemoryUse::Internal);
 *     // -> "SetCapacity" at ParticlePool.h:47
 *
 * A literal still works where the call site is not the useful name — one
 * function allocating three different things, say — and it wins over the
 * derived one:
 *
 *     buffer.Allocate(n, Deki::MemoryUse::Internal, "ParticlePool::posX");
 *
 * Nothing here is copied or allocated. Every member points at a string
 * literal the compiler emitted with static storage duration, which is what
 * makes it safe for the debug leak table to hold the pointer. Do not
 * construct one from a std::string: the label must outlive the allocation.
 */
struct AllocContext
{
    const char* label;  ///< what the caller called it, or nullptr

#if defined(DEKI_ALLOC_CONTEXT_CAPTURE)
    const char* file;      ///< __FILE__ of the call site
    const char* function;  ///< enclosing function, with its signature on GCC
    uint32_t line;

    /// Omitted: the call site names the allocation.
    AllocContext(std::source_location loc = std::source_location::current())
        : label(nullptr), file(loc.file_name()), function(loc.function_name()),
          line(loc.line())
    {
    }

    /// A literal: the caller's name wins, and the call site rides along so a
    /// failure can still say where. Implicit on purpose — it is what keeps
    /// every existing `..., "Sprite::Load")` call site compiling unchanged.
    AllocContext(const char* explicit_label,
                 std::source_location loc = std::source_location::current())
        : label(explicit_label), file(loc.file_name()),
          function(loc.function_name()), line(loc.line())
    {
    }

    const char* File() const { return file; }
    uint32_t Line() const { return line; }

    /// What the logs print: the caller's label when there is one, otherwise
    /// the function the allocation was written in.
    const char* Name() const { return label ? label : function; }

#else
    // Release and firmware: one pointer, which is exactly what the old
    // `const char* context` parameter was. No strings are captured, so a
    // label-less call site costs nothing and reports nothing.
    AllocContext() : label(nullptr) {}
    AllocContext(const char* explicit_label) : label(explicit_label) {}

    const char* File() const { return "?"; }
    uint32_t Line() const { return 0; }
    const char* Name() const { return label ? label : "?"; }
#endif
};

// The layout differs between debug and release builds, so the engine and the
// packages that call into it have to agree — the same constraint MemoryStats
// already imposes, and the same one the reflection ABI number exists to
// police.


/**
 * @brief Where an allocation lives. The caller says; the engine never guesses.
 *
 * Three regions, and every allocating call names one — there is no default and
 * omitting it does not compile. MemoryUse::External is the only route to PSRAM.
 *
 * Size says nothing about where a buffer belongs. A full-screen framebuffer
 * is the largest allocation on the part and also the one touched most, and it
 * belongs in internal RAM; a decoded asset the same size may never be read
 * again after load. Only the caller knows which it is holding, so only the
 * caller chooses.
 *
 * The placement is resolved once and NOT revisited. There is no fallback to
 * the other region when the chosen one is full: a failed allocation returns
 * nullptr, which every caller already has to handle. A board with no external
 * RAM resolves everything to internal — not a fallback, just the only region
 * there is.
 */
enum class MemoryUse : uint8_t
{
    /// Internal RAM. Fast, and the scarce one — a few hundred KB on an
    /// ESP32-S3. Where everything lives unless you say otherwise.
    Internal = 0,

    /// External RAM (PSRAM) where the board has it, internal where it does
    /// not. Megabytes, and markedly slower: it is reached over SPI, so a
    /// buffer read every frame does not belong here. Asking for External is a
    /// claim that this buffer's latency does not matter.
    External,

    /// Internal RAM that a DMA engine can reach.
    ///
    /// Not a synonym for Internal: on the ESP32-S3 some internal regions are
    /// not DMA-reachable, and a peripheral reading one of those gets silent
    /// corruption rather than an error. A framebuffer handed to a display
    /// driver needs this; a glyph table does not.
    DMA,
};

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
        uint32_t line;      ///< 0 when the context was a hand-written label
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
     * @param use Where it lives (see MemoryUse). Internal unless Buffer is
     *            named; resolved once, with no fallback to the other region
     * @param ctx Who is asking. Omit it — the compiler fills in the call site
     * @return Pointer to allocated memory, or nullptr if failed
     */
    static void* Allocate(size_t size,
                          MemoryUse use,
                          AllocContext ctx = {});

    /**
     * @brief Deprecated: pass a MemoryUse instead of choosing PSRAM yourself
     *
     * Kept so packages built against an older engine still compile. true maps
     * to MemoryUse::External and false to MemoryUse::Internal.
     */
    static void* Allocate(size_t size, bool prefer_psram, AllocContext ctx = {})
    {
        return Allocate(size, prefer_psram ? MemoryUse::External : MemoryUse::Internal, ctx);
    }

    // kLargeAllocation is gone with the size heuristic it served. Nothing
    // decides placement by size any more: a caller names MemoryUse::External or
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
    static T* AllocateArray(size_t count, MemoryUse use, AllocContext ctx = {})
    {
        if (count == 0)
            return nullptr;
        if (count > (SIZE_MAX / sizeof(T)))
            return nullptr;  // would wrap; no allocation could satisfy it anyway
        // ctx is forwarded, not defaulted again, so it still names the
        // caller of AllocateArray rather than this header.
        return static_cast<T*>(Allocate(count * sizeof(T), use, ctx));
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
