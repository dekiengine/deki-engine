#pragma once

#include <stdint.h>
#include <stddef.h>
#include <memory>

// Forward declaration

namespace Deki
{

class IMemoryProvider;

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
 * internal RAM or external PSRAM, and falls back when the first choice is
 * exhausted. A caller passing a placement directly cannot know how much
 * internal RAM is left, whether PSRAM exists on this board, or what else has
 * been allocated this frame — the engine can.
 */
enum class MemoryUse : uint8_t
{
    /// No opinion. The engine chooses by size: large buffers go external where
    /// PSRAM exists, small ones stay internal.
    Auto = 0,

    /// Read or written every frame. Internal RAM first; external only if
    /// internal is exhausted, since PSRAM access is markedly slower.
    Hot,

    /// Large and read-mostly — a gradient bake, a decoded image, an asset
    /// blob. External first, internal as a fallback.
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
     * to MemoryUse::Buffer and false to MemoryUse::Auto, and the engine may
     * still place the block elsewhere if that is the only way to satisfy it.
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
