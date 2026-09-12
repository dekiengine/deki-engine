#pragma once

#include <stdint.h>
#include <stddef.h>
#include <memory>

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
 * @brief What an allocation is FOR, so the engine can decide where it goes
 *
 * Callers say how memory will be used; Memory decides whether that means
 * internal RAM or external PSRAM. A caller passing a placement directly cannot
 * know whether this board has external RAM or how slow it is — the engine can.
 *
 * The placement is resolved once and NOT revisited. There is no fallback to
 * the other region when the chosen one is full: quietly moving a Hot buffer
 * into external RAM turns a placement mistake into frame drops nobody can
 * account for, and moving a Buffer the other way exhausts the internal heap
 * the next Hot allocation needed. A failed allocation returns nullptr, which
 * every caller already has to handle.
 *
 * A board with no external RAM resolves everything to internal. That is not a
 * fallback, it is the only region there is.
 */
enum class MemoryUse : uint8_t
{
    /// No opinion. The engine chooses by size: large buffers go external where
    /// PSRAM exists, small ones stay internal.
    Auto = 0,

    /// Read or written every frame. Internal RAM; external access is markedly
    /// slower, so this never silently ends up there.
    Hot,

    /// Large and read-mostly — a gradient bake, a decoded image, an asset
    /// blob. External where the board has it, internal where it does not.
    Buffer,

    /// Must be reachable by DMA. Internal only; never silently placed
    /// somewhere a peripheral cannot read.
    Dma,
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
     * @param use What the memory is for (see MemoryUse); the engine maps that
     *            to internal or external and falls back if the first is full
     * @param context Optional context string for debugging
     * @return Pointer to allocated memory, or nullptr if failed
     */
    static void* Allocate(size_t size,
                          MemoryUse use = MemoryUse::Auto,
                          const char* context = nullptr);

    /**
     * @brief Deprecated: pass a MemoryUse instead of choosing PSRAM yourself
     *
     * Kept so packages built against an older engine still compile. true maps
     * to MemoryUse::Buffer and false to MemoryUse::Auto.
     */
    static void* Allocate(size_t size, bool prefer_psram, const char* context = nullptr)
    {
        return Allocate(size, prefer_psram ? MemoryUse::Buffer : MemoryUse::Auto, context);
    }

    /// Size at or above which MemoryUse::Auto is treated as a Buffer.
    static constexpr size_t kLargeAllocation = 16 * 1024;

    /**
     * @brief Allocate memory from internal RAM only (never external/PSRAM)
     * @param size Size in bytes to allocate
     * @param context Optional context string for debugging
     * @return Pointer to allocated memory, or nullptr if failed
     */
    static void* AllocateInternal(size_t size, const char* context = nullptr);

    /**
     * @brief Free memory allocated via AllocateInternal
     * @param ptr Pointer to memory to free
     */
    static void FreeInternal(void* ptr);

    static void Free(void* ptr, const char* context = nullptr);

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
    static T* AllocateArray(size_t count, MemoryUse use = MemoryUse::Auto,
                            const char* context = nullptr)
    {
        if (count == 0)
            return nullptr;
        if (count > (SIZE_MAX / sizeof(T)))
            return nullptr;  // would wrap; no allocation could satisfy it anyway
        return static_cast<T*>(Allocate(count * sizeof(T), use, context));
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
    static uint32_t UpdateTracking(int32_t size, bool isPsram, void* ptr = nullptr, const char* context = nullptr);

#ifdef DEBUG
    /**
     * @brief Track allocation for leak detection (debug only)
     */
    static uint32_t TrackAllocation(void* ptr, size_t size, const char* context, bool isPsram);

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
