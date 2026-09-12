// Global operator new / delete for builds with exceptions off.
//
// WHAT THIS FIXES
//
// `new` cannot report failure on a device. ESP-IDF builds with
// -fno-exceptions, so libstdc++'s operator new calls std::__throw_bad_alloc,
// which with exceptions removed is a direct abort() — the board reboots with
// no message saying it ran out of memory. Worse, `new (std::nothrow) T[n]`
// aborts identically, because libstdc++ implements the nothrow form by
// calling the throwing form inside a try/catch that -fno-exceptions has
// deleted. A caller who did everything right still gets a reboot.
//
// Replacing the operators fixes both halves:
//
//   * a failed allocation says how many bytes it wanted, and how much heap
//     was left, before it aborts. That line is the difference between
//     "the board reboots sometimes" and a diagnosis;
//   * std::nothrow works. `new (std::nothrow) T[n]` returns nullptr, so code
//     that wants to handle exhaustion itself now can, and the trap documented
//     all over this engine stops being a trap.
//
// This also covers std::vector and std::string, which Deki::Buffer cannot:
// they call operator new internally, so until now their growth failures were
// the one remaining silent reboot.
//
// WHY IT STILL USES malloc, AND DOES NOT ROUTE THROUGH Deki::Memory
//
// Deliberate, and the docs say so too. Routing these through
// Memory::Allocate would cost real memory and real time on every std
// container operation:
//
//   * Memory::Allocate adds a header before the user pointer. Sixteen bytes
//     on every std::string that outgrows its small-string buffer, on a part
//     with a few hundred KB of heap;
//   * it zeroes what it returns, so every allocation would pay a memset that
//     a vector is about to overwrite;
//   * the ESP32 provider's AllocateInternal aligns to 16 bytes to let the
//     blit kernels use SIMD, which wastes up to 15 bytes per small block
//     against malloc's 8.
//
// And routing by size the way MemoryUse::Auto does would silently move a
// large std::vector into PSRAM, turning a hot loop slow for reasons nobody
// could see in the code. A caller that knows how memory will be used says so
// with a MemoryUse and gets Deki::Buffer; operator new has no way to know, so
// it does not guess.
//
// So this is instrumentation, not a re-route: same allocator, same alignment,
// same bytes, plus a diagnostic and a working nothrow. The success path is a
// malloc and a branch that libstdc++ already had.
//
// WHAT IS NOT REPLACED
//
// The over-aligned forms, operator new/delete(size_t, align_val_t). They stay
// libstdc++'s, which keeps them a matched pair (its aligned new uses
// aligned_alloc and its aligned delete the matching free). They are rare, and
// a mismatched pair here would be heap corruption rather than a missing log
// line.

#include <deki/LogSystem.h>

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
        return;
    s_Reporting = true;

    // Not through Deki::Memory's stats: this path has to work before the
    // memory backend is initialised and after it is gone.
    DEKI_LOG_ERROR("%s: out of memory asking for %zu bytes. Nothing is left to "
                   "allocate from; a buffer whose size comes from a file, a scene "
                   "or the screen should use Deki::Buffer<T>, which returns empty "
                   "instead of aborting",
                   what, bytes);

    s_Reporting = false;
}

}  // namespace

// --- throwing forms: cannot return null, so they say why and abort ---------

void* operator new(std::size_t size)
{
    // Zero-size still has to return a unique pointer, which malloc(0) may
    // not. One byte is what libstdc++ does too.
    void* p = std::malloc(size ? size : 1);
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

// --- nothrow forms: these now do what their name says ---------------------

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    void* p = std::malloc(size ? size : 1);
    if (!p)
        ReportOutOfMemory(size, "operator new(nothrow)");
    return p;  // nullptr, as the caller asked for
}

void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept
{
    return ::operator new(size, tag);
}

// --- the matching deletes -------------------------------------------------
// All of them, including the sized and nothrow forms: a delete the compiler
// can call that is not defined here would come from libstdc++ and pair its
// free with our malloc. They happen to match today, and relying on that is
// how a heap gets corrupted two releases from now.

void operator delete(void* ptr) noexcept
{
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept
{
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept
{
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept
{
    std::free(ptr);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept
{
    std::free(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept
{
    std::free(ptr);
}

#endif  // !__cpp_exceptions
