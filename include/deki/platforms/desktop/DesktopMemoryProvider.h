#pragma once

#include <deki/providers/IMemoryProvider.h>

// For GetRawBlockSize: the usable size of a malloc block, which has no
// portable spelling.
#if defined(_WIN32)
#include <malloc.h>
#elif defined(__GLIBC__) || defined(__linux__)
#include <malloc.h>
#endif

/**
 * @file DesktopMemoryProvider.h
 * @brief Desktop memory provider using malloc/free
 *
 * Implementation for desktop platforms without external RAM:
 * - Desktop (Windows, macOS, Linux)
 * - Simulator builds
 * - Editor builds
 *
 * All external RAM methods return false/0/nullptr since there's no
 * external memory to manage - everything uses standard heap.
 */

namespace Deki
{

class DesktopMemoryProvider : public IMemoryProvider
{
public:
    DesktopMemoryProvider() = default;
    ~DesktopMemoryProvider() override = default;

    bool Initialize() override { return true; }
    void Shutdown() override {}

    // A desktop has one heap and it answers every request, so this is the
    // whole provider. The same four lines are what a Switch or an Android
    // build would write: on a platform where all memory is equally fast and
    // equally reachable, "internal" and "external" are names for the same
    // place, and saying so explicitly is the honest answer.
    bool Serves(Memory::Region region) const override
    {
        return region == Deki::Memory::Internal || region == Deki::Memory::External;
    }

    void* Allocate(Memory::Region region, size_t bytes, bool needsDma) override
    {
        // needsDma is satisfied by construction: nothing here is behind a bus
        // a DMA engine cannot cross.
        (void)needsDma;
        return Serves(region) ? malloc(bytes) : nullptr;
    }

    void Free(Memory::Region region, void* ptr) override
    {
        (void)region;
        free(ptr);
    }

    // Deliberately unmeasured: zero here means "not a meaningful question on
    // this platform". A desktop's free memory is a question about the OS
    // rather than the process, and the number would be meaningless next to a
    // board's "280 KB of internal SRAM left".
    size_t GetAvailable(Memory::Region region) const override
    {
        (void)region;
        return 0;
    }

    // AllocateRaw/FreeRaw keep the base malloc/free. The size query has no
    // portable spelling, so it is per-platform: _msize on the Windows CRT,
    // malloc_usable_size on glibc and musl. It only feeds the reported byte
    // total, so a platform without one simply reports nothing rather than
    // guessing.
    size_t GetRawBlockSize(void* ptr) const override
    {
        if (!ptr)
            return 0;
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
