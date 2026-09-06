#pragma once

#include <deki/providers/IMemoryProvider.h>

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
};

}  // namespace Deki
