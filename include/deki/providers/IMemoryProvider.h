#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

/**
 * @file IMemoryProvider.h
 * @brief Abstract interface for platform-specific memory management
 *
 * This interface allows the engine to work with different MCU memory systems
 * without hardcoded platform-specific code. Implementations handle:
 * - ESP32: PSRAM via heap_caps
 * - RP2040: Standard malloc (no external RAM)
 * - Desktop: Standard malloc
 */

namespace Deki
{

class IMemoryProvider
{
public:
    virtual ~IMemoryProvider() = default;

    /**
     * @brief Initialize the memory backend
     * @return true if initialization succeeded
     */
    virtual bool Initialize() = 0;

    /**
     * @brief Shutdown the memory backend
     */
    virtual void Shutdown() = 0;

    // ========================================================================
    // External RAM Capabilities
    // ========================================================================

    /**
     * @brief Check if external RAM (PSRAM, SDRAM, etc.) is available
     * @return true if external RAM is present and usable
     */
    virtual bool HasExternalRAM() const = 0;

    /**
     * @brief Get total external RAM size in bytes
     * @return Total external RAM size, 0 if none available
     */
    virtual size_t GetTotalExternalRAM() const = 0;

    /**
     * @brief Get available (free) external RAM size in bytes
     * @return Available external RAM size, 0 if none available
     */
    virtual size_t GetAvailableExternalRAM() const = 0;

    // ========================================================================
    // External RAM Allocation
    // ========================================================================

    /**
     * @brief Allocate memory from external RAM
     * @param size Number of bytes to allocate
     * @return Pointer to allocated memory, nullptr if allocation failed
     */
    virtual void* AllocateExternal(size_t size) = 0;

    /**
     * @brief Free memory allocated from external RAM
     * @param ptr Pointer to memory to free (must have been allocated via AllocateExternal)
     */
    virtual void FreeExternal(void* ptr) = 0;

    /**
     * @brief Check if a pointer is in external RAM address range
     * @param ptr Pointer to check
     * @return true if pointer is in external RAM
     */
    virtual bool IsExternalPointer(void* ptr) const = 0;

    // ========================================================================
    // Internal RAM Allocation
    // ========================================================================

    /**
     * @brief Allocate memory from internal RAM only (never external)
     * @param size Number of bytes to allocate
     * @return Pointer to allocated memory, nullptr if allocation failed
     */
    virtual void* AllocateInternal(size_t size) { return malloc(size); }

    /**
     * @brief Allocate memory a DMA engine can read
     * @param size Number of bytes
     * @return Pointer, or nullptr
     *
     * Not the same as internal RAM: on the ESP32 some internal regions are not
     * DMA-reachable, so a framebuffer handed to a display peripheral needs the
     * explicit capability rather than "internal and probably fine". Freed
     * through FreeInternal. The default is AllocateInternal, which is right
     * for hosts where every allocation is equally reachable.
     */
    virtual void* AllocateDma(size_t size) { return AllocateInternal(size); }
    /// Frees a block from AllocateInternal. Must match that allocator: the
    /// default pair is malloc/free; a backend that overrides one overrides both.
    virtual void FreeInternal(void* ptr) { free(ptr); }

    /**
     * @brief Get available internal RAM size in bytes
     * @return Available internal RAM size, 0 if not supported
     */
    virtual size_t GetAvailableInternalRAM() const { return 0; }

    // ========================================================================
    // Raw Allocation (the operator new path)
    // ========================================================================
    //
    // The plain, natural-alignment allocator, for allocations the engine does
    // not get to describe: std::vector's storage, std::string's buffer, a
    // scalar `new`. The global operator new routes here, so that every
    // allocation on a device really does go through the provider and a board
    // can substitute its own heap for all of them.
    //
    // Separate from AllocateInternal on purpose. That one over-aligns to 16
    // bytes so the blit kernels can use SIMD, and ESP-IDF implements aligned
    // allocation by over-allocating — which would waste more per std::string
    // than the bookkeeping header this path exists to avoid. These carry no
    // header at all: the pointer handed out is the pointer the heap returned.

    /**
     * @brief Allocate with natural alignment and no engine bookkeeping
     * @param size Number of bytes
     * @return Pointer, or nullptr
     *
     * Must be at least as aligned as malloc, since operator new has to
     * satisfy alignof(std::max_align_t). The default IS malloc.
     */
    virtual void* AllocateRaw(size_t size) { return malloc(size); }

    /// Frees a block from AllocateRaw. Override both or neither.
    virtual void FreeRaw(void* ptr) { free(ptr); }

    /**
     * @brief Usable size of a block from AllocateRaw
     * @param ptr A pointer from AllocateRaw
     * @return Its size in bytes, or 0 where the platform cannot say
     *
     * There is no header to read it from, so the heap is asked instead
     * (heap_caps_get_allocated_size on ESP-IDF, _msize on Windows). Used only
     * to keep the byte total honest on free; returning 0 costs accuracy in
     * Memory::LogStats and nothing else.
     */
    virtual size_t GetRawBlockSize(void* ptr) const { (void)ptr; return 0; }
};

}  // namespace Deki
