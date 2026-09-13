#include <deki/platforms/esp32/ESP32MemoryProvider.h>

#if defined(ESP32)
// ESP-IDF heap capabilities API
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_idf_version.h"

// ESP-IDF 5.0+ provides esp_dma_malloc for proper DMA + cache alignment
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "esp_dma_utils.h"
#endif
#endif

namespace Deki
{

#if defined(ESP32)


bool ESP32MemoryProvider::Initialize()
{
    // Check if PSRAM is available
    size_t psramSize = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    m_HasPSRAM = (psramSize > 0);
    return true;
}

void ESP32MemoryProvider::Shutdown()
{
    // Nothing to clean up
}

bool ESP32MemoryProvider::HasExternalRAM() const
{
    return m_HasPSRAM;
}

size_t ESP32MemoryProvider::GetTotalExternalRAM() const
{
    return heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
}

size_t ESP32MemoryProvider::GetAvailableExternalRAM() const
{
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

void* ESP32MemoryProvider::AllocateExternal(size_t size)
{
    if (!m_HasPSRAM)
        return nullptr;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    // Use esp_dma_malloc for proper DMA capability AND cache-line alignment in PSRAM
    // This is critical for ESP32-S3 where PSRAM is cached:
    // - heap_caps_malloc with MALLOC_CAP_DMA only guarantees DMA-capable memory region
    // - esp_dma_malloc ensures both DMA capability AND cache-line alignment (32/64 bytes)
    // - Proper alignment prevents cache coherency issues when CPU and DMA access same memory
    void* ptr = nullptr;
    esp_err_t result = esp_dma_malloc(size, ESP_DMA_MALLOC_FLAG_PSRAM, &ptr, NULL);
    if (result == ESP_OK && ptr)
    {
        return ptr;
    }
    // Fall through to heap_caps_malloc if esp_dma_malloc fails
#endif

    // Fallback: Use MALLOC_CAP_DMA for basic cache alignment on ESP32-S3
    // This ensures alignment to cache line boundaries (32-64 bytes)
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
}

void ESP32MemoryProvider::FreeExternal(void* ptr)
{
    if (ptr)
    {
        heap_caps_free(ptr);
    }
}

bool ESP32MemoryProvider::IsExternalPointer(void* ptr) const
{
    if (!ptr)
        return false;

    // ESP32-S3 PSRAM address range: 0x3C000000 - 0x3E000000 (external RAM/SPIRAM)
    // ESP32 PSRAM address range: 0x3F800000 - 0x3FC00000
    // This check covers both variants
    intptr_t addr = reinterpret_cast<intptr_t>(ptr);

    // ESP32-S3 range
    if (addr >= 0x3C000000 && addr < 0x3E000000)
        return true;

    // ESP32 (original) range
    if (addr >= 0x3F800000 && addr < 0x3FC00000)
        return true;

    return false;
}

void* ESP32MemoryProvider::AllocateInternal(size_t size)
{
    // 16-byte alignment lets the QuadBlit dispatcher engage S3 PIE SIMD
    // kernels (and is also a multiple of every smaller alignment, so any
    // existing caller is unaffected). heap_caps_aligned_alloc'd memory is
    // freed with the standard heap_caps_free / free path, same as
    // heap_caps_malloc, so Memory::FreeInternal needs no change.
    return heap_caps_aligned_alloc(16, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void* ESP32MemoryProvider::AllocateDMA(size_t size)
{
    // MALLOC_CAP_DMA, explicitly. Internal RAM is mostly DMA-reachable on this
    // part but not all of it, and a display peripheral reading a framebuffer
    // that is not gets silent corruption rather than an error. Same 16-byte
    // alignment and the same heap_caps_free pairing as AllocateInternal.
    return heap_caps_aligned_alloc(16, size, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
}

void ESP32MemoryProvider::FreeInternal(void* ptr)
{
    // heap_caps_aligned_alloc pairs with heap_caps_free.
    if (ptr) heap_caps_free(ptr);
}

size_t ESP32MemoryProvider::GetAvailableInternalRAM() const
{
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

size_t ESP32MemoryProvider::GetRawBlockSize(void* ptr) const
{
    // Reads the block header the heap already keeps, so the raw path can
    // account for itself without carrying a header of its own. Usable size
    // rather than requested size, which is the truer footprint anyway.
    return ptr ? heap_caps_get_allocated_size(ptr) : 0;
}

#endif  // ESP32

}  // namespace Deki
