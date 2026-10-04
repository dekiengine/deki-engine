#include <deki/providers/Memory.h>
#include <deki/LogSystem.h>
#include <deki/providers/IMemoryProvider.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// Cache line alignment for ESP32-S3 (and most modern processors)
// This ensures allocated buffers don't cause cache coherency issues with DMA

namespace Deki
{

static constexpr size_t kCacheLineSize = 64;

// Bookkeeping stored in the 16 bytes right before every user pointer handed
// out by Allocate(). It carries which region the block came from, so Free()
// can hand it back to the right one without the caller remembering.
//
// Still 16 bytes, because the region id is paid for out of two fields that
// barely use theirs: headerSize is only ever 16 or a cache line, and
// trackSlot indexes at most a thousand debug records. Both fit in one word
// between them, which frees a whole word for the id. Every allocation on the
// device carries this header, so a 17th byte would be real memory.
struct AllocHeader
{
    uint32_t size;
    uint32_t packed;    ///< headerSize in the low 8 bits, trackSlot in the top 24
    uint32_t regionId;  ///< Memory::Region::id - where Free() returns it to
    uint32_t magic;

    uint32_t HeaderSize() const { return packed & 0xFFu; }
    uint32_t TrackSlot() const { return packed >> 8; }
    void SetPacked(uint32_t headerSize, uint32_t trackSlot) { packed = (headerSize & 0xFFu) | (trackSlot << 8); }
};
static_assert(sizeof(AllocHeader) == 16, "AllocHeader must be exactly 16 bytes");
static constexpr size_t kInternalHeaderSize = sizeof(AllocHeader);
static constexpr uint32_t kAllocMagic = 0xDEC1A11Cu;
static constexpr uint32_t kNoTrackSlot = 0xFFFFFFu;  // 24 bits, not 32

// __FILE__ is whatever path the build system fed the compiler, which here is
// absolute and long. The log wants the last component; this runs only on a
// failure or a leak, so the scan costs nothing that matters.
#if defined(DEKI_ALLOC_CONTEXT_CAPTURE)
static const char* BaseName(const char* path)
{
    if (!path)
    {
        return "?";
    }
    const char* last = path;
    for (const char* p = path; *p; ++p)
    {
        if (*p == '/' || *p == '\\')
        {
            last = p + 1;
        }
    }
    return last;
}
#endif

static inline AllocHeader* HeaderOf(void* userPtr)
{
    return reinterpret_cast<AllocHeader*>(static_cast<uint8_t*>(userPtr) - sizeof(AllocHeader));
}

// Static member definitions
std::unique_ptr<IMemoryProvider> Memory::s_CurrentBackend = nullptr;
bool Memory::s_Initialized = false;
bool Memory::s_ExternalAvailable = false;
bool Memory::s_ExternalEnabled = false;
uint32_t Memory::s_TotalAllocated = 0;
uint32_t Memory::s_ExternalAllocated = 0;
uint32_t Memory::s_InternalRamAllocated = 0;

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
    if (s_Initialized)
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
    s_ExternalAvailable = s_CurrentBackend->Serves(Deki::Memory::External);
    s_ExternalEnabled = s_ExternalAvailable;

    // Reset all counters
    s_TotalAllocated = 0;
    s_ExternalAllocated = 0;
    s_InternalRamAllocated = 0;
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

    s_Initialized = true;

    // What the board actually has, once, at startup. A project is sized
    // against these two numbers - whether a full-screen bake fits, whether a
    // particle pool has room - and without them the first out-of-memory line
    // arrives with nothing to compare against. ESP-IDF prints its heap
    // regions, but not which of them Deki will treat as external.
    // A host heap (editor, desktop simulator) reports 0 for "not measured",
    // which must not be printed as "0 bytes free".
    const size_t freeInternal = s_CurrentBackend->GetAvailable(Deki::Memory::Internal);
    if (freeInternal == 0)
    {
        DEKI_LOG_EDITOR("Memory: host heap, free space not measured");
    }
    else if (s_ExternalAvailable)
    {
        DEKI_LOG_INFO("Memory: %zu bytes free internal, %zu external "
                      "(only Deki::Memory::External goes external; everything else "
                      "stays internal)",
                      freeInternal, s_CurrentBackend->GetAvailable(Deki::Memory::External));
    }
    else
    {
        DEKI_LOG_INFO("Memory: %zu bytes free internal, no external RAM "
                      "(every region resolves to internal)",
                      freeInternal);
    }

    return true;
}

size_t Memory::GetAvailableInternal()
{
    return s_CurrentBackend ? s_CurrentBackend->GetAvailable(Deki::Memory::Internal) : 0;
}

// ---------------------------------------------------------------------------
// The raw path: operator new, and nothing else
// ---------------------------------------------------------------------------
//
// No header. The pointer handed out is the one the heap returned, so these
// allocations cost exactly what malloc costs - which is the point, since a
// std::string that outgrows its small-string buffer must not start paying 16
// bytes of engine bookkeeping. The size needed to keep the total honest on
// free comes from the heap instead (IMemoryProvider::GetRawBlockSize).
//
// What is given up with the header is the magic value, so a double free here
// cannot be diagnosed, as with plain malloc. Anything the engine can describe
// goes through Allocate, which keeps its guard.
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
// (Measure the configuration you ship: linking the engine's -O0 Debug objects
// against an -O2 caller makes the routing look twice as slow.)
static size_t s_RawAllocated = 0;

namespace Detail
{

void* RawAllocate(size_t size)
{
    // Before Initialize() there is no backend, and static initialisers do
    // allocate. malloc is what the default provider would have used anyway.
    if (!Memory::s_CurrentBackend)
    {
        return malloc(size);
    }

    void* ptr = Memory::s_CurrentBackend->AllocateRaw(size);
#if defined(DEKI_MEMORY_TRACK_RAW)
    if (ptr)
    {
        s_RawAllocated += Memory::s_CurrentBackend->GetRawBlockSize(ptr);
    }
#endif
    return ptr;
}

void RawFree(void* ptr)
{
    if (!ptr)
    {
        return;
    }
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

}  // namespace Detail

size_t Memory::GetRawAllocated()
{
    return s_RawAllocated;
}

void Memory::LogStats(const char* label)
{
    // One call answers "how much memory am I using?": the allocated side
    // comes from GetStats and the free side from a provider the caller does
    // not have.
    const MemoryStats s = GetStats();
    const size_t freeInternal = GetAvailableInternal();
    const size_t freeExternal = GetAvailableExternal();

    if (s_ExternalAvailable)
    {
        DEKI_LOG_INFO("Memory%s%s: %u bytes allocated (%u internal, %u external); "
                      "%zu free internal, %zu free external",
                      label ? " at " : "", label ? label : "", s.totalAllocated, s.internalRamAllocated,
                      s.externalAllocated, freeInternal, freeExternal);
    }
    else
    {
        DEKI_LOG_INFO("Memory%s%s: %u bytes allocated; %zu free internal", label ? " at " : "", label ? label : "",
                      s.totalAllocated, freeInternal);
    }

    // Reported separately because it is a different kind of number: the line
    // above is memory the engine placed and can account for exactly, this one
    // is what std::vector, std::string and scalar `new` are holding, measured
    // by asking the heap. Without it the figures above read as the whole
    // picture, which for a project with a few containers they are not.
    //
    // Said explicitly when it is off, rather than reporting a 0 that would
    // read as "no container memory" when it means "not counted".
#if defined(DEKI_MEMORY_TRACK_RAW)
    DEKI_LOG_INFO("Memory%s%s: %zu bytes in containers and scalar new", label ? " at " : "", label ? label : "",
                  s_RawAllocated);
#else
    DEKI_LOG_INFO("Memory%s%s: container and scalar-new bytes not counted "
                  "(build with DEKI_MEMORY_TRACK_RAW; it costs an allocator "
                  "size query per alloc and free)",
                  label ? " at " : "", label ? label : "");
#endif

#ifdef DEBUG
    DEKI_LOG_INFO("Memory%s%s: %u live allocations, peak %u", label ? " at " : "", label ? label : "",
                  s.activeAllocations, s.peakAllocations);
#endif
}

void Memory::Shutdown()
{
    if (!s_Initialized)
    {
        return;
    }

#ifdef DEBUG
    CheckForLeaks();
#else
    if (s_TotalAllocated > 0)
    {
        DEKI_LOG_WARNING("Memory leak detected: %u bytes still allocated at shutdown", s_TotalAllocated);
    }
#endif

    // Shutdown backend
    if (s_CurrentBackend)
    {
        s_CurrentBackend->Shutdown();
        s_CurrentBackend.reset();
    }

    // And forget that the board had external RAM. Otherwise this outlives the
    // backend that reported it, and once anything restarts the memory system a
    // Deki::Memory::External takes the external path, finds no provider, and
    // fails with "Failed to allocate N bytes in external RAM" on a machine
    // with gigabytes free.
    s_ExternalAvailable = false;
    s_ExternalEnabled = false;

    // Reset all tracking
    s_TotalAllocated = 0;
    s_ExternalAllocated = 0;
    s_InternalRamAllocated = 0;
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

    s_Initialized = false;
}

void Memory::SetBackend(IMemoryProvider* backend)
{
    s_CurrentBackend.reset(backend);
}

// Where a use lives on THIS board. Resolved once, and not revisited.
//
// Nothing reaches external RAM unless the caller named Deki::Memory::External.
// There is no size rule and no other way in: size does not predict placement,
// since the largest allocation on the part is usually the framebuffer and it
// belongs in internal RAM. A buffer that lands somewhere slow for reasons
// invisible in the source is a performance bug nobody can find by reading the
// code, so the engine asks instead of guessing.
//
// A board with no external RAM resolves everything to internal. That is not a
// fallback, it is the only region there is.
#ifdef DEBUG
// Two regions with the same id and different names would be indistinguishable:
// a buffer allocated from one would be freed to the other, because the header
// carries only the id. Two packages choosing the SAME name is fine and
// intended - that is how they agree on a vocabulary without coordinating - but
// two different names landing on one hash is a bug nobody would ever guess at.
//
// So the first time each id is seen, its name is remembered, and a later
// mismatch says so. Sixteen slots and a pointer compare first: region names are
// literals, so the usual case is one comparison and no strcmp at all.
static void CheckRegionIdentity(Deki::Memory::Region region)
{
    struct Seen
    {
        uint32_t id;
        const char* name;
    };
    static Seen s_Seen[16];
    static int s_Count = 0;

    for (int i = 0; i < s_Count; ++i)
    {
        if (s_Seen[i].id != region.id)
        {
            continue;
        }
        if (s_Seen[i].name == region.name || strcmp(s_Seen[i].name, region.name) == 0)
        {
            return;  // same region, same name: nothing to say
        }
        DEKI_LOG_ERROR("Memory: regions '%s' and '%s' hash to the same id (%u). "
                       "One of them must be renamed - the allocation header "
                       "stores only the id, so blocks from one would be freed "
                       "to the other",
                       s_Seen[i].name, region.name, region.id);
        return;
    }
    if (s_Count < static_cast<int>(sizeof(s_Seen) / sizeof(s_Seen[0])))
    {
        s_Seen[s_Count++] = Seen{ region.id, region.name };
    }
}
#endif

void* Memory::Allocate(size_t size, Memory::Region region, AllocContext ctx)
{
    return AllocateIn(size, region, /*needsDma=*/false, ctx);
}

void* Memory::AllocateDma(size_t size, Memory::Region region, AllocContext ctx)
{
    return AllocateIn(size, region, /*needsDma=*/true, ctx);
}

// The one allocation path. `needsDma` is the constraint the public AllocateDma
// carries, and it is orthogonal to the region: a framebuffer must be reachable
// whether the board keeps it internally or in PSRAM, so neither placement can
// silently drop the requirement.
void* Memory::AllocateIn(size_t size, Memory::Region region, bool needsDma, AllocContext ctx)
{
#ifdef DEBUG
    CheckRegionIdentity(region);
#endif

    if (size == 0)
    {
        DEKI_LOG_WARNING("Attempted to allocate 0 bytes");
        return nullptr;
    }
    // A size read from a corrupt file can be anything. Near SIZE_MAX the
    // header and rounding below would wrap to a small total on a 32-bit board,
    // and the caller would then write its full size into that. The header
    // also records the size in 32 bits.
    if (size > UINT32_MAX || size > SIZE_MAX - 2 * kCacheLineSize)
    {
        DEKI_LOG_ERROR("Refused to allocate %zu bytes: larger than any allocation can be (a corrupt size?)", size);
        return nullptr;
    }

    // The header sits immediately before the user pointer (see AllocHeader).
    // External (PSRAM) buffers are DMA targets on the ESP32-S3, where cache
    // operations work on 64-byte lines, so they keep a cache-line-sized header
    // and a cache-line-rounded payload. Internal allocations take a 16-byte
    // header and no rounding: giving them the cache-line treatment would cost
    // a minimum of 128 bytes for a 16-byte request, on a part with a few
    // hundred KB of RAM.
    void* rawPtr = nullptr;
    size_t headerSize = kInternalHeaderSize;
    size_t totalSize = size + headerSize;

    // A board with no external RAM has exactly one region, so an External
    // request resolves to Internal there. That is not a fallback - there is no
    // second region to fall back to.
    const bool externalUsable = s_ExternalAvailable && s_ExternalEnabled;
    const Memory::Region placed =
        (region == Deki::Memory::External && !externalUsable) ? Deki::Memory::Internal : region;
    const bool usedExternal = (placed == Deki::Memory::External);

    // External buffers are DMA targets on the ESP32-S3, where cache operations
    // work on 64-byte lines, so they take a cache-line-sized header and a
    // cache-line-rounded payload.
    if (usedExternal)
    {
        const size_t alignedSize = (size + kCacheLineSize - 1) & ~(kCacheLineSize - 1);
        headerSize = kCacheLineSize;
        totalSize = alignedSize + kCacheLineSize;
    }

    // One region, one attempt. Substituting another when the chosen one is
    // full turns a placement mistake into frame drops nobody can account for,
    // or exhausts the heap the next allocation needed. A region this board does
    // not serve returns nullptr the same way a full one does, and the failure
    // below names it.
    if (s_CurrentBackend)
    {
        rawPtr = s_CurrentBackend->Allocate(placed, totalSize, needsDma);
    }
    else
    {
        rawPtr = malloc(totalSize);  // host builds, before a backend is set
    }

    if (rawPtr)
    {
        void* userPtr = static_cast<uint8_t*>(rawPtr) + headerSize;
        AllocHeader* header = HeaderOf(userPtr);
        header->size = static_cast<uint32_t>(size);
        header->regionId = placed.id;
        header->magic = kAllocMagic;

        // Zero initialize only the user portion
        memset(userPtr, 0, size);

        // Update tracking with user-requested size (and remember the slot so
        // Free() releases it without a search)
        header->SetPacked(static_cast<uint32_t>(headerSize),
                          UpdateTracking(static_cast<int32_t>(size), usedExternal, userPtr, ctx.Name(), ctx.Line()));

        return userPtr;
    }
    else
    {
        // How much was free is the number that makes this actionable: it is
        // the difference between "this asset is too big for this board" and
        // "something else is holding memory it should have released". Only
        // computed on the failure path, so it costs nothing when allocating
        // succeeds - and the region it reports is the one that was tried,
        // since there is no fallback to the other.
        //
        // GetAvailable returns 0 when a provider does not
        // implement it (the desktop one does not - the number is meaningless
        // where the OS pages), so a zero is reported as unknown rather than
        // as an empty heap, which would send someone hunting a leak that is
        // not there.
        const size_t freeHere = s_CurrentBackend ? s_CurrentBackend->GetAvailable(region) : 0;
        // The region's own name, not a guess from a bool: a board that does
        // not serve "rtc" has to say "rtc", or the message sends a reader
        // looking at the wrong memory entirely.
        const char* regionName = placed.name;

        // Every word here is one a reader needs: what, how big, which
        // region, how much was left in it, and who asked. The placement rule
        // is printed once at startup rather than restated on every failure.
#if defined(DEKI_ALLOC_CONTEXT_CAPTURE)
        if (freeHere > 0)
        {
            DEKI_LOG_ERROR("Failed to allocate %zu bytes in '%s' for '%s' at %s:%u "
                           "(%zu bytes free there)",
                           size, regionName, ctx.Name(), BaseName(ctx.File()), ctx.Line(), freeHere);
        }
        else
        {
            DEKI_LOG_ERROR("Failed to allocate %zu bytes in '%s' for '%s' at %s:%u", size, regionName, ctx.Name(),
                           BaseName(ctx.File()), ctx.Line());
        }
#else
        // No call site was captured, so none is claimed - and the line says how
        // to get one instead of spending its width on the no-fallback rule,
        // which is in docs/memory.md and does not change between runs. Someone
        // reading this on a board wants to know where to look next, and when
        // the caller passed no label at all ('?') that is the only lead there
        // is.
        if (freeHere > 0)
        {
            DEKI_LOG_ERROR("Failed to allocate %zu bytes in '%s' for '%s' "
                           "(%zu bytes free there); build with DEBUG for the call site",
                           size, regionName, ctx.Name(), freeHere);
        }
        else
        {
            DEKI_LOG_ERROR("Failed to allocate %zu bytes in '%s' for '%s'; "
                           "build with DEBUG for the call site",
                           size, regionName, ctx.Name());
        }
#endif
    }

    return nullptr;
}

void* Memory::AllocateInternal(size_t size, AllocContext ctx)
{
    if (size == 0)
    {
        return nullptr;
    }

    void* ptr = nullptr;
    if (s_CurrentBackend)
    {
        ptr = s_CurrentBackend->Allocate(Deki::Memory::Internal, size, /*needsDma=*/false);
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
#if defined(DEKI_ALLOC_CONTEXT_CAPTURE)
        DEKI_LOG_ERROR("Failed to allocate %zu bytes in internal RAM for '%s' at %s:%u", size, ctx.Name(),
                       BaseName(ctx.File()), ctx.Line());
#else
        DEKI_LOG_ERROR("Failed to allocate %zu bytes in internal RAM for '%s'; "
                       "build with DEBUG for the call site",
                       size, ctx.Name());
#endif
    }

    return ptr;
}

void Memory::FreeInternal(void* ptr)
{
    if (!ptr)
    {
        return;
    }
    // Symmetric with AllocateInternal: the backend that handed the block out
    // frees it (heap_caps_free on the ESP32); plain free() would be correct
    // only by luck of the backend's implementation.
    if (s_CurrentBackend)
    {
        s_CurrentBackend->Free(Deki::Memory::Internal, ptr);
    }
    else
    {
        free(ptr);
    }
}

void Memory::Free(void* ptr, AllocContext ctx)
{
    if (!ptr)
    {
        return;
    }

    // The header right before the user pointer says where the raw block starts.
    AllocHeader* header = HeaderOf(ptr);
    if (header->magic != kAllocMagic)
    {
        DEKI_LOG_ERROR("DekiMemory::Free: %p was not allocated by DekiMemory::Allocate (%s)", ptr, ctx.Name());
        return;
    }
    header->magic = 0;  // double free shows up as the error above, not as heap corruption
    void* rawPtr = static_cast<uint8_t*>(ptr) - header->HeaderSize();
    const size_t freedSize = header->size;

#ifdef DEBUG
    UntrackAllocation(ptr, header->TrackSlot());
#endif

    // The block says which region it came from, so the provider need not
    // recognise its own pointer, and an internal block goes back through the
    // provider that allocated it rather than plain free() (which works only
    // because heap_caps and free happen to share a heap on ESP-IDF).
    //
    // The name is not stored (it would cost a pointer per allocation); a
    // provider dispatches on the id, and only a failure would want the name.
    const Memory::Region region{ header->regionId, "?" };
    const bool wasExternal = (header->regionId == Deki::Memory::External.id);

    if (s_CurrentBackend)
    {
        s_CurrentBackend->Free(region, rawPtr);
    }
    else
    {
        free(rawPtr);
    }

    // Always update tracking (both DEBUG and RELEASE builds)
    UpdateTracking(-static_cast<int32_t>(freedSize), wasExternal, ptr, ctx.Name(), ctx.Line());
}

MemoryStats Memory::GetStats()
{
    MemoryStats stats;
    stats.totalAllocated = s_TotalAllocated;
    stats.externalAllocated = s_ExternalAllocated;
    stats.internalRamAllocated = s_InternalRamAllocated;
#ifdef DEBUG
    stats.activeAllocations = activeAllocations;
    stats.peakAllocations = peakAllocations;
#endif
    return stats;
}

uint32_t Memory::UpdateTracking(int32_t size, bool isExternal, void* ptr, const char* context, uint32_t line)
{
    // Update global counters only - no subsystem overhead
    s_TotalAllocated += size;
    if (isExternal)
    {
        s_ExternalAllocated += size;
    }
    else
    {
        s_InternalRamAllocated += size;
    }

#ifdef DEBUG
    if (size > 0 && ptr)
    {
        return TrackAllocation(ptr, static_cast<size_t>(size), context, isExternal);
    }
#else
    (void)ptr;
    (void)context;
#endif
    return kNoTrackSlot;
}

size_t Memory::GetAvailableExternal()
{
    return s_CurrentBackend ? s_CurrentBackend->GetAvailable(Deki::Memory::External) : 0;
}

bool Memory::IsInitialized()
{
    return s_Initialized;
}

bool Memory::IsExternalAvailable()
{
    return s_ExternalAvailable;
}

#ifdef DEBUG
uint32_t Memory::TrackAllocation(void* ptr, size_t size, const char* context, uint32_t line, bool isExternal)
{
    if (!s_AllocationRecords || !ptr)
    {
        return kNoTrackSlot;
    }

    // Free slots are kept on a stack, so tracking is O(1) instead of a scan of
    // every record on every allocation and free. The caller stores the slot in
    // the allocation header.
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
            DEKI_LOG_WARNING("Allocation tracking full (%u records) - later allocations are not tracked; the leak "
                             "report is now partial",
                             (unsigned)s_MaxAllocations);
        }
        return kNoTrackSlot;
    }

    s_AllocationRecords[slot].ptr = ptr;
    s_AllocationRecords[slot].size = size;
    s_AllocationRecords[slot].context = context;
    s_AllocationRecords[slot].isExternal = isExternal;
    activeAllocations++;
    if (activeAllocations > peakAllocations)
    {
        peakAllocations = activeAllocations;
    }
    return slot;
}

void Memory::UntrackAllocation(void* ptr, uint32_t slot)
{
    if (!s_AllocationRecords || !ptr || slot == kNoTrackSlot || slot >= s_MaxAllocations)
    {
        return;
    }
    if (s_AllocationRecords[slot].ptr != ptr)
    {
        return;  // stale slot (tracking was reset)
    }

    s_AllocationRecords[slot].ptr = nullptr;
    s_AllocationRecords[slot].size = 0;
    s_AllocationRecords[slot].context = nullptr;
    s_AllocationRecords[slot].isExternal = false;
    activeAllocations--;
    if (s_FreeSlotCount < s_MaxAllocations)
    {
        s_FreeSlots[s_FreeSlotCount++] = slot;
    }
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
            const char* location = s_AllocationRecords[i].isExternal ? "External" : "Internal";
            DEKI_LOG_ERROR("  LEAK: %p - %zu bytes [%s] from %s:%u", s_AllocationRecords[i].ptr,
                           s_AllocationRecords[i].size, location, context, s_AllocationRecords[i].line);
        }
    }

    DEKI_LOG_WARNING("=== MEMORY LEAK SUMMARY ===");
    DEKI_LOG_ERROR("Engine: %u leaks, %u bytes", leak_count, leaked_bytes);
    DEKI_LOG_WARNING("Total: %u allocations, %u bytes", activeAllocations, leaked_bytes);
}
#endif

}  // namespace Deki
