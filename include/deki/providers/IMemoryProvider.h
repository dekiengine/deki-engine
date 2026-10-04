#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

// For Memory::Region. Memory.h only forward-declares IMemoryProvider, so
// including it here makes no cycle.
#include <deki/providers/Memory.h>

namespace Deki
{

/// The platform's memory, as the engine sees it, so the engine has no
/// platform-specific allocation code: a microcontroller with external RAM
/// uses its SDK's capability allocator, a part without it or a desktop uses
/// malloc.
class IMemoryProvider
{
public:
    virtual ~IMemoryProvider() = default;

    /// Returns false on failure.
    virtual bool Initialize() = 0;

    virtual void Shutdown() = 0;

    // ========================================================================
    // Regions
    // ========================================================================

    /// True when the board can place memory in `region`. A platform with one
    /// heap answers true for every region it can satisfy: where all memory is
    /// equally fast, "external" without PSRAM is the same place as
    /// "internal". Memory asks once at startup to report what the board has.
    virtual bool Serves(Memory::Region region) const = 0;

    /// Allocates `bytes` from `region`, or returns nullptr, including for a
    /// region the board does not serve; it never substitutes another region.
    /// `needsDma` means a DMA engine must be able to read the block, in any
    /// region: a framebuffer must be reachable in internal RAM or PSRAM alike.
    ///
    /// One entry point, not a virtual per region, because packages can define
    /// new regions (such as "rtc") without an engine release. A provider
    /// switches on `region.id`, a compile-time constant, and returns nullptr
    /// for any it does not know.
    virtual void* Allocate(Memory::Region region, size_t bytes, bool needsDma) = 0;

    /// Frees a block from `region`. The region comes from the allocation's own
    /// header, so callers never pass it themselves.
    virtual void Free(Memory::Region region, void* ptr) = 0;

    /// Bytes still free in `region`, or 0 when the platform cannot measure it.
    /// It appears in the startup line and in every out-of-memory message, so a
    /// provider returning 0 should say in a comment that 0 means "unknown",
    /// not "exhausted".
    virtual size_t GetAvailable(Memory::Region region) const = 0;

    // Raw allocation, which the global operator new goes through: no engine
    // header, the pointer handed out is the pointer the heap returned.

    /// Allocates `size` bytes, or returns nullptr. Must be at least as aligned
    /// as malloc, since operator new must satisfy alignof(std::max_align_t).
    /// The default is malloc.
    virtual void* AllocateRaw(size_t size) { return malloc(size); }

    /// Frees a block from AllocateRaw. Override both or neither.
    virtual void FreeRaw(void* ptr) { free(ptr); }

    /// Usable size of a block from AllocateRaw, or 0 when the platform cannot
    /// say. There is no header, so the heap is asked
    /// (heap_caps_get_allocated_size on ESP-IDF, _msize on Windows). Only
    /// keeps the byte total right on free; 0 costs accuracy in
    /// Memory::LogStats and nothing else.
    virtual size_t GetRawBlockSize(void* ptr) const
    {
        (void)ptr;
        return 0;
    }
};

}  // namespace Deki
