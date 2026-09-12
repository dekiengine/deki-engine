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
