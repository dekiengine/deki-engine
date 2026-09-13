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

    // No external RAM on desktop platforms
    bool HasExternalRAM() const override { return false; }
    size_t GetTotalExternalRAM() const override { return 0; }
    size_t GetAvailableExternalRAM() const override { return 0; }

    // External allocation always fails - use standard malloc instead
    void* AllocateExternal(size_t size) override
    {
        (void)size;
        return nullptr;
    }
    void FreeExternal(void* ptr) override { (void)ptr; }
    bool IsExternalPointer(void* ptr) const override
    {
        (void)ptr;
        return false;
    }

    // The three regions, spelled out. On a desktop they are all the C heap:
    // there is no separate internal pool and no DMA reachability to respect,
    // so saying malloc three times is the honest answer rather than a
    // shortcut.
    void* AllocateInternal(size_t size) override { return malloc(size); }
    void FreeInternal(void* ptr) override { free(ptr); }

    // No DMA engine is involved on a desktop, so every pointer is equally
    // reachable and plain malloc satisfies the contract.
    void* AllocateDMA(size_t size) override { return malloc(size); }

    // Deliberately unmeasured: zero here means "not a meaningful question on
    // this platform". A desktop's free memory is a question about the OS
    // rather than the process, and the number would be meaningless next to a
    // board's "280 KB of internal SRAM left".
    size_t GetAvailableInternalRAM() const override { return 0; }

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
