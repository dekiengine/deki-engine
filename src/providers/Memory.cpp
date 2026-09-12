#include <deki/providers/Memory.h>
#include <deki/LogSystem.h>
#include <deki/providers/IMemoryProvider.h>
#include <cstdlib>
#include <cstring>

// Cache line alignment for ESP32-S3 (and most modern processors)
// This ensures allocated buffers don't cause cache coherency issues with DMA

namespace Deki
{

static constexpr size_t CACHE_LINE_SIZE = 64;

// Bookkeeping stored in the 16 bytes right before every user pointer handed
// out by Allocate(). headerSize says how far back the raw allocation starts
// (16 for internal RAM, a cache line for PSRAM), trackSlot is the debug
// tracking record so Free() can release it in O(1).
struct AllocHeader
{
    uint32_t size;
    uint32_t headerSize;
    uint32_t trackSlot;
    uint32_t magic;
};
static_assert(sizeof(AllocHeader) == 16, "AllocHeader must be exactly 16 bytes");
static constexpr size_t kInternalHeaderSize = sizeof(AllocHeader);
static constexpr uint32_t kAllocMagic = 0xDEC1A11Cu;
static constexpr uint32_t kNoTrackSlot = 0xFFFFFFFFu;

static inline AllocHeader* HeaderOf(void* userPtr)
{
    return reinterpret_cast<AllocHeader*>(static_cast<uint8_t*>(userPtr) - sizeof(AllocHeader));
}

// Static member definitions
std::unique_ptr<IMemoryProvider> Memory::s_CurrentBackend = nullptr;
bool Memory::initialized = false;
bool Memory::s_PsramAvailable = false;
bool Memory::s_PsramEnabled = false;
uint32_t Memory::totalAllocated = 0;
uint32_t Memory::psramAllocated = 0;
uint32_t Memory::internalRamAllocated = 0;

#ifdef DEBUG
Memory::AllocationRecord* Memory::s_AllocationRecords = nullptr;
uint32_t* Memory::s_FreeSlots = nullptr;
uint32_t Memory::s_FreeSlotCount = 0;
uint32_t Memory::s_NextFreshSlot = 0;
bool Memory::s_TrackingFullReported = false;
uint32_t Memory::s_MaxAllocations = 1024;
uint32_t Memory::activeAllocations = 0;
uint32_t Memory::peakAllocations = 0;
uint32_t Memory::s_NextAllocationId = 1;
#endif

bool Memory::Initialize()
{
    if (initialized)
    {
        return true;
    }

#ifdef DEBUG
    // Allocate tracking array (using malloc to avoid recursion)
    s_AllocationRecords = (AllocationRecord*)malloc(s_MaxAllocations * sizeof(AllocationRecord));
    s_FreeSlots = (uint32_t*)malloc(s_MaxAllocations * sizeof(uint32_t));
    if (s_AllocationRecords)
    {
        memset(s_AllocationRecords, 0, s_MaxAllocations * sizeof(AllocationRecord));
        s_FreeSlotCount = 0;
        s_NextFreshSlot = 0;
        s_TrackingFullReported = false;
    }
#endif

    // Backend must be set via SetBackend() before Initialize()
    if (!s_CurrentBackend || !s_CurrentBackend->Initialize())
    {
        DEKI_LOG_ERROR("Failed to initialize memory backend");
        return false;
    }

    // Detect external RAM availability via backend
    s_PsramAvailable = s_CurrentBackend->HasExternalRAM();
    s_PsramEnabled = s_PsramAvailable;

    // Reset all counters
    totalAllocated = 0;
    psramAllocated = 0;
    internalRamAllocated = 0;
#ifdef DEBUG
    activeAllocations = 0;
    peakAllocations = 0;
    s_NextAllocationId = 1;
    if (s_AllocationRecords)
    {
        memset(s_AllocationRecords, 0, s_MaxAllocations * sizeof(AllocationRecord));
        s_FreeSlotCount = 0;
        s_NextFreshSlot = 0;
        s_TrackingFullReported = false;
    }
#endif

    initialized = true;
    return true;
}

void Memory::Shutdown()
{
    if (!initialized)
    {
        return;
    }

#ifdef DEBUG
    CheckForLeaks();
#else
    if (totalAllocated > 0)
    {
        DEKI_LOG_WARNING("Memory leak detected: %u bytes still allocated at shutdown", totalAllocated);
    }
#endif

    // Shutdown backend
    if (s_CurrentBackend)
    {
        s_CurrentBackend->Shutdown();
        s_CurrentBackend.reset();
    }

    // Reset all tracking
    totalAllocated = 0;
    psramAllocated = 0;
    internalRamAllocated = 0;
#ifdef DEBUG
    activeAllocations = 0;
    peakAllocations = 0;
    if (s_AllocationRecords)
    {
        memset(s_AllocationRecords, 0, s_MaxAllocations * sizeof(AllocationRecord));
        s_FreeSlotCount = 0;
        s_NextFreshSlot = 0;
        s_TrackingFullReported = false;
        free(s_AllocationRecords);
        free(s_FreeSlots);
        s_FreeSlots = nullptr;
        s_AllocationRecords = nullptr;
    }
#endif

    initialized = false;
}

void Memory::SetBackend(IMemoryProvider* backend)
{
    s_CurrentBackend.reset(backend);
}

// Where a use should live, given this board and what is left.
//
// This is the whole point of taking a MemoryUse rather than a placement: the
// caller knows how the memory will be used, and only the engine knows whether
// there is PSRAM, how much internal RAM is free, and how slow external access
// is on this part.
static bool PreferExternalFor(Deki::MemoryUse use, size_t size, bool psramUsable)
{
    if (!psramUsable)
        return false;  // nothing to prefer
    switch (use)
    {
        case Deki::MemoryUse::Dma:    return false;  // a peripheral must reach it
        case Deki::MemoryUse::Hot:    return false;  // touched every frame
        case Deki::MemoryUse::Buffer: return true;
        case Deki::MemoryUse::Auto:
        default:
            return size >= Deki::Memory::kLargeAllocation;
    }
}

void* Memory::Allocate(size_t size,
                           MemoryUse use,
                           const char* context)
{
    if (size == 0)
    {
        DEKI_LOG_WARNING("Attempted to allocate 0 bytes");
        return nullptr;
    }

    // The header sits immediately before the user pointer (see AllocHeader).
    // External (PSRAM) buffers are DMA targets on the ESP32-S3, where cache
    // operations work on 64-byte lines, so they keep a cache-line-sized header
    // and a cache-line-rounded payload. Internal allocations do not need that:
    // they used to pay the same 64-byte header plus up-to-63 bytes of rounding,
    // i.e. a minimum of 128 bytes for a 16-byte request, on a part with a few
    // hundred KB of RAM.
    void* raw_ptr = nullptr;
    bool used_psram = false;
    size_t header_size = kInternalHeaderSize;
    size_t total_size = size + header_size;

    // The engine decides, from the use and what this board has.
    const bool psramUsable = s_PsramAvailable && s_PsramEnabled;
    const bool prefer_psram = PreferExternalFor(use, size, psramUsable);

    if (prefer_psram)
    {
        const size_t aligned_size = (size + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1);
        raw_ptr = AllocatePSRAM(aligned_size + CACHE_LINE_SIZE);
        if (raw_ptr)
        {
            used_psram = true;
            header_size = CACHE_LINE_SIZE;
            total_size = aligned_size + CACHE_LINE_SIZE;
        }
    }

    // Internal, when external was not preferred or could not satisfy it.
    if (!raw_ptr)
    {
        raw_ptr = malloc(total_size);
        used_psram = false;
    }

    // And external as a last resort for a use that did not ask for it. A Hot
    // buffer in slow memory still beats not having it; a Dma one does not,
    // because a peripheral could not read it.
    if (!raw_ptr && psramUsable && !prefer_psram && use != MemoryUse::Dma)
    {
        const size_t aligned_size = (size + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1);
        raw_ptr = AllocatePSRAM(aligned_size + CACHE_LINE_SIZE);
        if (raw_ptr)
        {
            used_psram = true;
            header_size = CACHE_LINE_SIZE;
            total_size = aligned_size + CACHE_LINE_SIZE;
            DEKI_LOG_WARNING("Memory: %zu bytes for '%s' went to PSRAM; internal RAM is full",
                             size, context ? context : "?");
        }
    }

    if (raw_ptr)
    {
        void* user_ptr = static_cast<uint8_t*>(raw_ptr) + header_size;
        AllocHeader* header = HeaderOf(user_ptr);
        header->size = static_cast<uint32_t>(size);
        header->headerSize = static_cast<uint32_t>(header_size);
        header->magic = kAllocMagic;

        // Zero initialize only the user portion
        memset(user_ptr, 0, size);

        // Update tracking with user-requested size (and remember the slot so
        // Free() releases it without a search)
        header->trackSlot = UpdateTracking(static_cast<int32_t>(size), used_psram, user_ptr, context);

        return user_ptr;
    }
    else
    {
        DEKI_LOG_ERROR("Failed to allocate %zu bytes", size);
    }

    return nullptr;
}

void* Memory::AllocateInternal(size_t size, const char* context)
{
    if (size == 0) return nullptr;

    void* ptr = nullptr;
    if (s_CurrentBackend)
    {
        ptr = s_CurrentBackend->AllocateInternal(size);
    }
    else
    {
        ptr = malloc(size);
    }

    if (ptr)
    {
        memset(ptr, 0, size);
    }
    else
    {
        DEKI_LOG_ERROR("Failed to allocate %zu bytes in internal RAM (%s)", size, context ? context : "");
    }

    return ptr;
}

void Memory::FreeInternal(void* ptr)
{
    if (!ptr) return;
    // Symmetric with AllocateInternal: the backend that handed the block out
    // frees it (heap_caps_free on the ESP32); plain free() was only correct
    // by luck of the backend's implementation.
    if (s_CurrentBackend)
        s_CurrentBackend->FreeInternal(ptr);
    else
        free(ptr);
}

void Memory::Free(void* ptr, const char* context)
{
    if (!ptr)
    {
        return;
    }

    // The header right before the user pointer says where the raw block starts.
    AllocHeader* header = HeaderOf(ptr);
    if (header->magic != kAllocMagic)
    {
        DEKI_LOG_ERROR("DekiMemory::Free: %p was not allocated by DekiMemory::Allocate (%s)",
                       ptr, context ? context : "");
        return;
    }
    header->magic = 0;  // double free shows up as the error above, not as heap corruption
    void* raw_ptr = static_cast<uint8_t*>(ptr) - header->headerSize;
    const size_t freed_size = header->size;

#ifdef DEBUG
    UntrackAllocation(ptr, header->trackSlot);
#endif

    // Use backend to check if pointer is in external RAM (check raw_ptr, not user ptr)
    bool was_psram = false;
    if (s_CurrentBackend && s_CurrentBackend->IsExternalPointer(raw_ptr))
    {
        s_CurrentBackend->FreeExternal(raw_ptr);
        was_psram = true;
    }
    else
    {
        free(raw_ptr);
    }

    // Always update tracking (both DEBUG and RELEASE builds)
    UpdateTracking(-static_cast<int32_t>(freed_size), was_psram, ptr, context);
}

MemoryStats Memory::GetStats()
{
    MemoryStats stats;
    stats.totalAllocated = totalAllocated;
    stats.psramAllocated = psramAllocated;
    stats.internalRamAllocated = internalRamAllocated;
#ifdef DEBUG
    stats.activeAllocations = activeAllocations;
    stats.peakAllocations = peakAllocations;
#endif
    return stats;
}

bool Memory::DetectPSRAM()
{
    return s_CurrentBackend ? s_CurrentBackend->HasExternalRAM() : false;
}

void* Memory::AllocatePSRAM(size_t size)
{
    return s_CurrentBackend ? s_CurrentBackend->AllocateExternal(size) : nullptr;
}

void Memory::FreePSRAM(void* ptr)
{
    if (s_CurrentBackend)
    {
        s_CurrentBackend->FreeExternal(ptr);
    }
}

uint32_t Memory::UpdateTracking(int32_t size, bool isPsram, void* ptr, const char* context)
{
    // Update global counters only - no subsystem overhead
    totalAllocated += size;
    if (isPsram)
    {
        psramAllocated += size;
    }
    else
    {
        internalRamAllocated += size;
    }

#ifdef DEBUG
    if (size > 0 && ptr)
    {
        return TrackAllocation(ptr, static_cast<size_t>(size), context, isPsram);
    }
#else
    (void)ptr;
    (void)context;
#endif
    return kNoTrackSlot;
}

size_t Memory::GetAvailablePSRAM()
{
    return s_CurrentBackend ? s_CurrentBackend->GetAvailableExternalRAM() : 0;
}

bool Memory::IsInitialized()
{
    return initialized;
}

bool Memory::IsPSRAMAvailable()
{
    return s_PsramAvailable;
}

#ifdef DEBUG
uint32_t Memory::TrackAllocation(void* ptr, size_t size, const char* context, bool isPsram)
{
    if (!s_AllocationRecords || !ptr) return kNoTrackSlot;

    // Free slots are kept on a stack, so tracking is O(1) instead of a scan of
    // every record on every allocation (and every free, which searched by
    // pointer). The caller stores the slot in the allocation header.
    uint32_t slot;
    if (s_FreeSlotCount > 0)
    {
        slot = s_FreeSlots[--s_FreeSlotCount];
    }
    else if (s_NextFreshSlot < s_MaxAllocations)
    {
        slot = s_NextFreshSlot++;
    }
    else
    {
        // Tracking is full. Said once, not once per allocation: this fires on
        // a hot path, and the leak report it protects is already incomplete.
        if (!s_TrackingFullReported)
        {
            s_TrackingFullReported = true;
            DEKI_LOG_WARNING("Allocation tracking full (%u records) - later allocations are not tracked; the leak report is now partial",
                             (unsigned)s_MaxAllocations);
        }
        return kNoTrackSlot;
    }

    s_AllocationRecords[slot].ptr = ptr;
    s_AllocationRecords[slot].size = size;
    s_AllocationRecords[slot].context = context;
    s_AllocationRecords[slot].isPsram = isPsram;
    activeAllocations++;
    if (activeAllocations > peakAllocations)
    {
        peakAllocations = activeAllocations;
    }
    return slot;
}

void Memory::UntrackAllocation(void* ptr, uint32_t slot)
{
    if (!s_AllocationRecords || !ptr || slot == kNoTrackSlot || slot >= s_MaxAllocations) return;
    if (s_AllocationRecords[slot].ptr != ptr) return;  // stale slot (tracking was reset)

    s_AllocationRecords[slot].ptr = nullptr;
    s_AllocationRecords[slot].size = 0;
    s_AllocationRecords[slot].context = nullptr;
    s_AllocationRecords[slot].isPsram = false;
    activeAllocations--;
    if (s_FreeSlotCount < s_MaxAllocations)
        s_FreeSlots[s_FreeSlotCount++] = slot;
}

void Memory::CheckForLeaks()
{
    if (activeAllocations == 0)
    {
        return;
    }

    // Count and report leaks
    uint32_t leaked_bytes = 0;
    uint32_t leak_count = 0;

    for (uint32_t i = 0; i < s_MaxAllocations; i++)
    {
        if (s_AllocationRecords[i].ptr)
        {
            leaked_bytes += s_AllocationRecords[i].size;
            leak_count++;
            const char* context = s_AllocationRecords[i].context ? s_AllocationRecords[i].context : "unknown";
            const char* location = s_AllocationRecords[i].isPsram ? "PSRAM" : "Internal";
            DEKI_LOG_ERROR("  LEAK: %p - %zu bytes [%s] from %s",
                           s_AllocationRecords[i].ptr, s_AllocationRecords[i].size, location, context);
        }
    }

    DEKI_LOG_WARNING("=== MEMORY LEAK SUMMARY ===");
    DEKI_LOG_ERROR("Engine: %u leaks, %u bytes", leak_count, leaked_bytes);
    DEKI_LOG_WARNING("Total: %u allocations, %u bytes", activeAllocations, leaked_bytes);
}
#endif

}  // namespace Deki
