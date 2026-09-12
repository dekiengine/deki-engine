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
};

}  // namespace Deki
