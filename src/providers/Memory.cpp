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

    // What the board actually has, once, at startup. A project is sized
    // against these two numbers — whether a full-screen bake fits, whether a
    // particle pool has room — and without them the first out-of-memory line
    // arrives with nothing to compare against. ESP-IDF prints its heap
    // regions, but not which of them Deki will treat as external.
    const size_t freeInternal = s_CurrentBackend->GetAvailableInternalRAM();
    if (s_PsramAvailable)
        DEKI_LOG_INFO("Memory: %zu bytes free internal, %zu external "
                      "(only MemoryUse::Buffer goes external; everything else "
                      "stays internal)",
                      freeInternal, s_CurrentBackend->GetAvailableExternalRAM());
    else
        DEKI_LOG_INFO("Memory: %zu bytes free internal, no external RAM "
                      "(every MemoryUse resolves to internal)",
                      freeInternal);

    return true;
}

size_t Memory::GetAvailableInternal()
{
    return s_CurrentBackend ? s_CurrentBackend->GetAvailableInternalRAM() : 0;
}

// ---------------------------------------------------------------------------
// The raw path: operator new, and nothing else
// ---------------------------------------------------------------------------
//
// No header. The pointer handed out is the one the heap returned, so these
// allocations cost exactly what malloc costs — which is the point, since a
// std::string that outgrows its small-string buffer must not start paying 16
// bytes of engine bookkeeping. The size needed to keep the total honest on
// free comes from the heap instead (IMemoryProvider::GetRawBlockSize).
//
// What is given up with the header is the magic value, so a double free here
// is as undiagnosable as it was when this path was plain malloc. Anything the
// engine can describe goes through Allocate, which keeps its guard.
//
// BYTE ACCOUNTING IS OPT-IN, AND BOTH HALVES WERE MEASURED
//
// 1.6M paired 48-byte allocations, each variant in its own process so the
// first measured does not pay the heap warm-up for the rest, engine compiled
// at -O2 like the firmware:
//
//   malloc + free                      47-52 ns/op
//   routed, accounting off             46-51 ns/op   (indistinguishable)
//   routed + DEKI_MEMORY_TRACK_RAW     63-71 ns/op   (+35%)
//
// So the routing itself is free: the provider call disappears into the noise,
// and separate controls confirmed a cross-TU call and a virtual call through
// a base pointer each cost nothing measurable. What is not free is counting
// the bytes, because with no header the block size has to come from the heap,
// and on Windows that is _msize calling HeapSize and taking a lock.
//
// Hence the split: every build routes, and only a build that asks counts.
// Turn DEKI_MEMORY_TRACK_RAW on while hunting where memory went, and accept a
// third more time per allocation while you look.
//
// (An earlier run of this benchmark said the routing cost 2x. It was linking
// the engine's Debug objects, which are -O0, against an -O2 caller. Measure
// the configuration you ship.)
static size_t s_RawAllocated = 0;

namespace detail
{

void* RawAllocate(size_t size)
{
    // Before Initialize() there is no backend, and static initialisers do
    // allocate. malloc is what the default provider would have used anyway.
    if (!Memory::s_CurrentBackend)
        return malloc(size);

    void* ptr = Memory::s_CurrentBackend->AllocateRaw(size);
#if defined(DEKI_MEMORY_TRACK_RAW)
    if (ptr)
        s_RawAllocated += Memory::s_CurrentBackend->GetRawBlockSize(ptr);
#endif
    return ptr;
}

void RawFree(void* ptr)
{
    if (!ptr)
        return;
    if (!Memory::s_CurrentBackend)
    {
        free(ptr);
        return;
    }

#if defined(DEKI_MEMORY_TRACK_RAW)
    // Clamped, not wrapped: a block allocated before the backend existed is
    // freed through it afterwards, and a total that underflowed would read as
    // several gigabytes and look like a catastrophic leak.
    const size_t block = Memory::s_CurrentBackend->GetRawBlockSize(ptr);
    s_RawAllocated = (block < s_RawAllocated) ? (s_RawAllocated - block) : 0;
#endif
    Memory::s_CurrentBackend->FreeRaw(ptr);
}

}  // namespace detail

size_t Memory::GetRawAllocated()
{
    return s_RawAllocated;
}

void Memory::LogStats(const char* label)
{
    // One call, because the question "how much memory am I using?" needed
    // GetStats for the allocated side and a provider the caller does not have
    // for the free side. Answering it should not be a research task.
    const MemoryStats s = GetStats();
    const size_t freeInternal = GetAvailableInternal();
    const size_t freeExternal = GetAvailablePSRAM();

    if (s_PsramAvailable)
        DEKI_LOG_INFO("Memory%s%s: %u bytes allocated (%u internal, %u external); "
                      "%zu free internal, %zu free external",
                      label ? " at " : "", label ? label : "",
                      s.totalAllocated, s.internalRamAllocated, s.psramAllocated,
                      freeInternal, freeExternal);
    else
        DEKI_LOG_INFO("Memory%s%s: %u bytes allocated; %zu free internal",
                      label ? " at " : "", label ? label : "",
                      s.totalAllocated, freeInternal);

    // Reported separately because it is a different kind of number: the line
    // above is memory the engine placed and can account for exactly, this one
    // is what std::vector, std::string and scalar `new` are holding, measured
    // by asking the heap. Without it the figures above read as the whole
    // picture, which for a project with a few containers they are not.
    //
    // Said explicitly when it is off, rather than reporting a 0 that would
    // read as "no container memory" when it means "not counted".
#if defined(DEKI_MEMORY_TRACK_RAW)
    DEKI_LOG_INFO("Memory%s%s: %zu bytes in containers and scalar new",
                  label ? " at " : "", label ? label : "", s_RawAllocated);
#else
    DEKI_LOG_INFO("Memory%s%s: container and scalar-new bytes not counted "
                  "(build with DEKI_MEMORY_TRACK_RAW; it costs an allocator "
                  "size query per alloc and free)",
                  label ? " at " : "", label ? label : "");
#endif

#ifdef DEBUG
    DEKI_LOG_INFO("Memory%s%s: %u live allocations, peak %u",
                  label ? " at " : "", label ? label : "",
                  s.activeAllocations, s.peakAllocations);
#endif
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

    // And forget that the board had external RAM. These outlived the backend
    // that reported them, so after a shutdown the engine still believed PSRAM
    // was there: a later MemoryUse::Buffer took the external path, found no
    // provider, and failed with "Failed to allocate N bytes in external RAM"
    // on a machine with gigabytes free. Harmless while shutdown only ever ran
    // at exit; not harmless once anything restarts the memory system.
    s_PsramAvailable = false;
    s_PsramEnabled = false;

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

// Where a use lives on THIS board. Resolved once, and not revisited.
//
// Nothing reaches external RAM unless the caller named MemoryUse::Buffer.
// There is no size rule and no other way in. The engine used to infer one —
// anything at or above 16 KB went to PSRAM — and that inference was wrong in
// the direction that hurts: in a graphics engine the largest allocations are
// usually the ones touched every frame, and a framebuffer in PSRAM measures
// markedly worse on real hardware than one in internal RAM. Guessing quietly
// is worse than asking, because a buffer that ends up somewhere slow for
// reasons invisible in the source is a performance bug nobody can find by
// reading the code.
//
// A board with no external RAM resolves everything to internal. That is not a
// fallback, it is the only region there is.
static bool ResolveExternal(Deki::MemoryUse use, bool psramUsable)
{
    if (!psramUsable)
        return false;
    return use == Deki::MemoryUse::Buffer;
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
    size_t header_size = kInternalHeaderSize;
    size_t total_size = size + header_size;

    const bool psramUsable = s_PsramAvailable && s_PsramEnabled;
    const bool used_psram = ResolveExternal(use, psramUsable);

    // One region, no second attempt. Quietly moving a Hot buffer into external
    // RAM because internal was full turns a placement mistake into frame drops
    // nobody can account for, and moving a Buffer the other way exhausts the
    // internal heap that the next Hot allocation needed. Failing here is
    // visible, and the caller already has to handle null.
    if (used_psram)
    {
        const size_t aligned_size = (size + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1);
        header_size = CACHE_LINE_SIZE;
        total_size = aligned_size + CACHE_LINE_SIZE;
        raw_ptr = AllocatePSRAM(total_size);
    }
    else if (s_CurrentBackend)
    {
        // Through the backend, not malloc: on the ESP32 that is heap_caps with
        // the right capability, and it is what FreeInternal pairs with. Plain
        // malloc here was only correct by luck of the backend.
        //
        // Dma asks for the capability explicitly. Internal RAM is mostly
        // DMA-reachable on that part but not all of it, and a peripheral
        // reading memory that is not gets silent corruption.
        raw_ptr = (use == MemoryUse::Dma) ? s_CurrentBackend->AllocateDma(total_size)
                                          : s_CurrentBackend->AllocateInternal(total_size);
    }
    else
    {
        raw_ptr = malloc(total_size);  // host builds, before a backend is set
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
        // How much was free is the number that makes this actionable: it is
        // the difference between "this asset is too big for this board" and
        // "something else is holding memory it should have released". Only
        // computed on the failure path, so it costs nothing when allocating
        // succeeds — and the region it reports is the one that was tried,
        // since there is no fallback to the other.
        //
        // GetAvailableInternalRAM returns 0 when a provider does not
        // implement it (the desktop one does not — the number is meaningless
        // where the OS pages), so a zero is reported as unknown rather than
        // as an empty heap, which would send someone hunting a leak that is
        // not there.
        const size_t freeHere = s_CurrentBackend
            ? (used_psram ? s_CurrentBackend->GetAvailableExternalRAM()
                          : s_CurrentBackend->GetAvailableInternalRAM())
            : 0;
        const char* region = used_psram ? "external" : "internal";
        if (freeHere > 0)
            DEKI_LOG_ERROR("Failed to allocate %zu bytes in %s RAM for '%s' "
                           "(%zu bytes free there); no fallback is attempted",
                           size, region, context ? context : "?", freeHere);
        else
            DEKI_LOG_ERROR("Failed to allocate %zu bytes in %s RAM for '%s'; "
                           "no fallback is attempted",
                           size, region, context ? context : "?");
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
