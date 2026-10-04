#pragma once

#include <deki/providers/IMemoryProvider.h>

// For GetRawBlockSize: the usable size of a malloc block, which has no
// portable spelling.
#if defined(_WIN32)
#include <malloc.h>
#elif defined(__GLIBC__) || defined(__linux__)
#include <malloc.h>
#endif

namespace Deki
{

/// Memory provider over the C heap (malloc/free), for any host with a normal
/// C library heap: the editor, desktop and simulator builds, tests.
class HostMemoryProvider : public IMemoryProvider
{
public:
    HostMemoryProvider() = default;
    ~HostMemoryProvider() override = default;

    bool Initialize() override { return true; }
    void Shutdown() override {}

    // One heap serves every request. Where all memory is equally fast and
    // reachable, "internal" and "external" name the same place.
    bool Serves(Memory::Region region) const override
    {
        return region == Deki::Memory::Internal || region == Deki::Memory::External;
    }

    void* Allocate(Memory::Region region, size_t bytes, bool needsDma) override
    {
        // Always met: no host memory is out of a DMA engine's reach.
        (void)needsDma;
        return Serves(region) ? malloc(bytes) : nullptr;
    }

    void Free(Memory::Region region, void* ptr) override
    {
        (void)region;
        free(ptr);
    }

    // 0 means "not measured": a host's free memory belongs to the OS, not the
    // process, and would mean nothing next to a board's internal SRAM left.
    size_t GetAvailable(Memory::Region region) const override
    {
        (void)region;
        return 0;
    }

    // AllocateRaw/FreeRaw keep the base malloc/free. The size query is
    // per-platform: _msize on the Windows CRT, malloc_usable_size on glibc and
    // musl. It only feeds the reported byte total, so elsewhere it reports 0.
    size_t GetRawBlockSize(void* ptr) const override
    {
        if (!ptr)
        {
            return 0;
        }
#if defined(_WIN32)
        return _msize(ptr);
#elif defined(__GLIBC__) || defined(__linux__)
        return malloc_usable_size(ptr);
#else
        return 0;
#endif
    }
};

}  // namespace Deki
