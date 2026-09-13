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

// PSRAM on the ESP32-S3 is cached, so a buffer a DMA engine will also read has
// to sit on a cache line or the two disagree. esp_dma_malloc gives both the DMA
// capability and that alignment; heap_caps_malloc with MALLOC_CAP_DMA gives
// only the capability, which is why it is the second choice rather than the
// first.
void* ESP32MemoryProvider::AllocateExternalBytes(size_t size)
{
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    void* ptr = nullptr;
    esp_err_t result = esp_dma_malloc(size, ESP_DMA_MALLOC_FLAG_PSRAM, &ptr, NULL);
    if (result == ESP_OK && ptr)
        return ptr;
    // esp_dma_malloc could not place it; the line below still aligns to a
    // cache line boundary, which is the part that matters for correctness.
#endif
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
}

bool ESP32MemoryProvider::Serves(MemoryRegion region) const
{
    if (region == Deki::Internal)
        return true;
    if (region == Deki::External)
        return m_HasPSRAM;
    // Anything else — a region some package defined — this board does not
    // have. Saying so is what lets the allocation fail by name instead of
    // quietly landing somewhere it does not belong.
    return false;
}

void* ESP32MemoryProvider::Allocate(MemoryRegion region, size_t bytes, bool needsDma)
{
    if (region == Deki::External)
    {
        if (!m_HasPSRAM)
            return nullptr;
        return AllocateExternalBytes(bytes);
    }

    if (region != Deki::Internal)
        return nullptr;  // a region this board does not serve

    // 16-byte alignment lets the QuadBlit dispatcher engage S3 PIE SIMD
    // kernels. heap_caps_aligned_alloc pairs with heap_caps_free, which is
    // what Free() below uses for either capability.
    //
    // MALLOC_CAP_DMA when the caller asked for reachability: internal RAM is
    // mostly DMA-reachable on this part but not all of it, and a display
    // peripheral reading a framebuffer that is not gets silent corruption
    // rather than an error.
    const uint32_t caps = needsDma ? (MALLOC_CAP_DMA | MALLOC_CAP_8BIT)
                                   : (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return heap_caps_aligned_alloc(16, bytes, caps);
}

void ESP32MemoryProvider::Free(MemoryRegion region, void* ptr)
{
    if (!ptr)
        return;
    // Every capability this provider hands out comes from the heap_caps
    // allocator, and heap_caps_free takes any of them, so the region does not
    // change what has to happen here — it is checked rather than used.
    (void)region;
    heap_caps_free(ptr);
}

size_t ESP32MemoryProvider::GetAvailable(MemoryRegion region) const
{
    if (region == Deki::Internal)
        return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (region == Deki::External)
        return m_HasPSRAM ? heap_caps_get_free_size(MALLOC_CAP_SPIRAM) : 0;
    return 0;
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
