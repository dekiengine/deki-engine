#pragma once

#include <deki/providers/IMemoryProvider.h>

/**
 * @file ESP32MemoryProvider.h
 * @brief ESP32 memory provider implementation using heap_caps API
 *
 * Provides PSRAM (SPI RAM) support for ESP32, ESP32-S2, ESP32-S3, etc.
 * Uses ESP-IDF heap_caps functions for external memory allocation.
 */

namespace Deki
{

#if defined(ESP32)

class ESP32MemoryProvider : public IMemoryProvider
{
public:
    ESP32MemoryProvider() = default;
    ~ESP32MemoryProvider() override = default;

    bool Initialize() override;
    void Shutdown() override;

    bool Serves(Memory::Region region) const override;
    void* Allocate(Memory::Region region, size_t bytes, bool needsDma) override;
    void Free(Memory::Region region, void* ptr) override;
    size_t GetAvailable(Memory::Region region) const override;

    // AllocateRaw/FreeRaw keep the base malloc/free, which on ESP-IDF is
    // heap_caps under the hood with the same alignment operator new had
    // before it was routed here. Only the size query needs the platform.
    size_t GetRawBlockSize(void* ptr) const override;

private:
    /// PSRAM, cache-line aligned so a DMA engine and the CPU agree about it.
    void* AllocateExternalBytes(size_t size);

private:
    bool m_HasPSRAM = false;
};

#endif  // ESP32

}  // namespace Deki
