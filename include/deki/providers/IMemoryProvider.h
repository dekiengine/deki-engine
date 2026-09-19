#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

// For Memory::Region. Memory.h only forward-declares IMemoryProvider, so this
// direction of the dependency is the safe one.
#include <deki/providers/Memory.h>

/**
 * @file IMemoryProvider.h
 * @brief Abstract interface for platform-specific memory management
 *
 * This interface allows the engine to work with different MCU memory systems
 * without hardcoded platform-specific code. Implementations handle:
 * - a microcontroller with external RAM: its SDK's capability allocator
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
    /**
     * @brief Does this board have `region` as a distinct place to put memory?
     *
     * A platform with one heap answers true for the regions it can satisfy and
     * false for the rest — and answering true for "external" when there is no
     * PSRAM is not a lie if every allocation is equally fast, it is a statement
     * that the distinction does not exist here.
     *
     * Memory asks this once at startup to report what the board has.
     */
    virtual bool Serves(Memory::Region region) const = 0;

    /**
     * @brief Allocate `bytes` from `region`
     * @param needsDma The caller requires a DMA engine to be able to read it.
     *                 Orthogonal to the region: a framebuffer must be reachable
     *                 whether it lives in internal RAM or in PSRAM.
     * @return The block, or nullptr — including for a region this board does
     *         not serve. There is no substitution.
     *
     * One entry point rather than one virtual per region, because the set of
     * regions is open: a package can define "rtc" without an engine release,
     * and no interface with a method per region could carry that. A provider
     * dispatches on `region.id`, which is a compile-time constant, and returns
     * nullptr for anything it does not recognise.
     */
    virtual void* Allocate(Memory::Region region, size_t bytes, bool needsDma) = 0;

    /**
     * @brief Release a block obtained from `region`
     *
     * The region comes from the allocation's own header, so a caller never has
     * to remember it and cannot get it wrong.
     */
    virtual void Free(Memory::Region region, void* ptr) = 0;

    /**
     * @brief Bytes still available in `region`, or 0 where the platform cannot
     *        say
     *
     * Reported in the startup line and in every out-of-memory message, which
     * is why a provider that cannot measure should say so in a comment rather
     * than let 0 read as "exhausted".
     */
    virtual size_t GetAvailable(Memory::Region region) const = 0;

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
