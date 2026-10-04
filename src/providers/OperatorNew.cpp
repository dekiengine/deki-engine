// Global operator new / delete for builds with exceptions off.
//
// WHY
//
// ESP-IDF builds with -fno-exceptions, so libstdc++'s operator new calls
// std::__throw_bad_alloc, which with exceptions removed is a direct abort():
// the board reboots with no message saying it ran out of memory. And
// `new (std::nothrow) T[n]` aborts the same way, because libstdc++ implements
// the nothrow form by calling the throwing form inside a try/catch that
// -fno-exceptions deletes.
//
// With these operators:
//
//   * a failed allocation logs how many bytes it wanted, and how much heap
//     was left, before it aborts. That line turns "the board reboots
//     sometimes" into a diagnosis;
//   * std::nothrow works: `new (std::nothrow) T[n]` returns nullptr, so code
//     can handle exhaustion itself.
//
// This also covers std::vector and std::string, which Deki::Buffer cannot:
// they call operator new internally, so their growth failures are logged too.
//
// HOW IT GOES THROUGH THE ENGINE AT NO COST
//
// Through Deki::Detail::RawAllocate, which is the provider's AllocateRaw, so
// a board sees every allocation on the device and can substitute its own heap
// for all of them, and the engine can report what containers are holding.
//
// It is NOT Memory::Allocate, and that distinction is the whole design:
//
//   * Memory::Allocate puts a 16-byte header before the user pointer. On
//     every std::string that outgrows its small-string buffer, on a part with
//     a few hundred KB of heap;
//   * it zeroes what it returns, so every allocation would pay a memset that
//     a vector is about to overwrite;
//   * its internal path over-aligns to 16 bytes on ESP32 so the blit kernels
//     can use SIMD, and ESP-IDF implements aligned allocation by
//     over-allocating - more waste than the header.
//
// AllocateRaw has none of that: natural alignment, no header, the pointer the
// heap returned. The bytes are accounted anyway by asking the heap for the
// block size (GetRawBlockSize), which is what the header would have been for.
//
// What is given up is the header's magic value, so a double free here cannot
// be diagnosed, as with plain malloc. Anything the engine can describe goes
// through Allocate and keeps the guard.
//
// Placement is deliberately NOT decided here: these allocations are internal,
// like everything a caller has not explicitly asked to place elsewhere.
// Routing by size would move a large std::vector into PSRAM and make a hot
// loop slow for reasons invisible in the code. A caller who wants external
// memory names Deki::Memory::External and gets Deki::Buffer; operator new cannot
// know what it is allocating for, so it does not guess.
//
// WHAT IS NOT REPLACED
//
// The over-aligned forms, operator new/delete(size_t, align_val_t). They stay
// libstdc++'s, which keeps them a matched pair (its aligned new uses
// aligned_alloc and its aligned delete the matching free). They are rare, and
// a mismatched pair here would be heap corruption rather than a missing log
// line.

#include <deki/LogSystem.h>
#include <deki/providers/Memory.h>

#include <cstddef>
#include <cstdlib>
#include <new>

#if !defined(__cpp_exceptions)

namespace
{

// Reporting formats a message and may reach a log callback that allocates. If
// that allocation also fails we would be back in here, so the second entry
// stays quiet rather than recursing until the stack runs out.
bool s_Reporting = false;

void ReportOutOfMemory(std::size_t bytes, const char* what)
{
    if (s_Reporting)
    {
        return;
    }
    s_Reporting = true;

    // Both numbers, for the same reason Memory::Allocate reports them: the
    // size alone cannot tell you whether the request was unreasonable or the
    // heap was already gone. Reads only, safe on a path this far down.
    DEKI_LOG_ERROR("%s: out of memory asking for %zu bytes (%zu free internal, "
                   "%zu held by containers). A buffer whose size comes from a "
                   "file, a scene or the screen should use Deki::Buffer<T>, "
                   "which returns empty instead of aborting",
                   what, bytes, Deki::Memory::GetAvailableInternal(), Deki::Memory::GetRawAllocated());

    s_Reporting = false;
}

}  // namespace

// --- throwing forms: cannot return null, so they say why and abort ---------

void* operator new(std::size_t size)
{
    // Zero-size still has to return a unique pointer, which malloc(0) may
    // not. One byte is what libstdc++ does too.
    void* p = Deki::Detail::RawAllocate(size ? size : 1);
    if (!p)
    {
        ReportOutOfMemory(size, "operator new");
        std::abort();  // -fno-exceptions leaves nothing else; at least it is explained
    }
    return p;
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

// --- nothrow forms: these return nullptr on failure -----------------------

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    void* p = Deki::Detail::RawAllocate(size ? size : 1);
    if (!p)
    {
        ReportOutOfMemory(size, "operator new(nothrow)");
    }
    return p;  // nullptr, as the caller asked for
}

void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept
{
    return ::operator new(size, tag);
}

// --- the matching deletes -------------------------------------------------
// All of them, including the sized and nothrow forms: a delete the compiler
// can call that is not defined here would come from libstdc++ and pair its
// free with our allocator. They happen to match, and relying on that would
// corrupt the heap the day they stop matching.

void operator delete(void* ptr) noexcept
{
    Deki::Detail::RawFree(ptr);
}

void operator delete[](void* ptr) noexcept
{
    Deki::Detail::RawFree(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept
{
    Deki::Detail::RawFree(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept
{
    Deki::Detail::RawFree(ptr);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept
{
    Deki::Detail::RawFree(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept
{
    Deki::Detail::RawFree(ptr);
}

#endif  // !__cpp_exceptions
