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
     * @brief Allocate memory
     * @param size Size in bytes to allocate
     * @param prefer_psram Whether to prefer PSRAM for this allocation
     * @param context Optional context string for debugging
     * @return Pointer to allocated memory, or nullptr if failed
     */
    static void* Allocate(size_t size,
                          bool prefer_psram = false,
                          const char* context = nullptr);

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
