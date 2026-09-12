#pragma once

// An owning buffer. Allocates through Deki::Memory, frees itself.
//
// This is the type component and package code should reach for instead of
// calling an allocator and remembering a matching Free. Every memory bug this
// engine has actually had was an ownership bug rather than a churn bug: a
// delete[] paired with a Deki::Memory allocation, a buffer nothing ever freed,
// a cleanup path that freed something a borrowing function did not own, a null
// pointer that reached a memcpy because one error branch forgot to return.
// A destructor cannot make any of those mistakes.
//
// WHY NOT AN ARENA
// A frame arena would remove the failure handling too, but it has to be sized
// up front. One large enough for a single full-screen gradient bake reserves
// 150 KB whether or not a project has a gradient, which on a part with a few
// hundred KB of heap is half the memory held against a feature that may not
// exist. Arenas pay off where there is headroom. Most allocations here are
// long-lived and owned by something anyway — glyph tables, pixel data, particle
// columns, framebuffers — which is what RAII serves and an arena does not.
//
// WHAT IS NOT AUTOMATED
// Whether the memory was available. `if (!buf)` once, where the buffer is
// created, rather than an unwind on every branch after it.

#include <deki/providers/Memory.h>

#include <stddef.h>
#include <utility>
#include <type_traits>

namespace Deki
{

namespace detail
{
// Deliberately not templates: a Buffer<T> would otherwise emit a copy of the
// allocate and free logic per element type, and this engine is built for parts
// where code size is a budget. What the template generates is a cast and a
// multiply.
void* BufferAllocate(size_t bytes, MemoryUse use, const char* context);
void BufferFree(void* ptr);
}  // namespace detail

/**
 * @brief Owning array of `T`, freed when it goes out of scope
 *
 * Move-only, like every other owning handle: copying would need a second
 * allocation that could fail, which a copy constructor cannot report.
 *
 * T must be trivially copyable. Nothing here constructs or destroys elements;
 * these are the raw pixel, glyph and sample buffers the engine passes to
 * blitters and drivers, and Memory zeroes what it hands back.
 */
template <typename T>
class Buffer
{
    // Enforced, not merely documented: nothing here constructs or destroys
    // elements, so a T with a destructor would have it silently never run.
    // These are the raw pixel, glyph and sample buffers the engine hands to
    // blitters and drivers, and Memory zeroes what it returns.
    static_assert(std::is_trivially_copyable<T>::value,
                  "Deki::Buffer<T> is for trivially copyable T; it does not "
                  "construct or copy elements");
    static_assert(std::is_trivially_destructible<T>::value,
                  "Deki::Buffer<T> does not destroy elements; a T with a "
                  "destructor would leak whatever that destructor releases");

public:
    Buffer() = default;

    /// Allocates `count` elements. Check the result with `if (!buffer)`.
    Buffer(size_t count, MemoryUse use = MemoryUse::Auto, const char* context = nullptr)
    {
        Allocate(count, use, context);
    }

    ~Buffer() { Reset(); }

    Buffer(Buffer&& other) noexcept
        : m_Data(other.m_Data), m_Count(other.m_Count)
    {
        other.m_Data = nullptr;
        other.m_Count = 0;
    }

    Buffer& operator=(Buffer&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_Data = other.m_Data;
            m_Count = other.m_Count;
            other.m_Data = nullptr;
            other.m_Count = 0;
        }
        return *this;
    }

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    /**
     * @brief (Re)allocate to `count` elements, returning whether it worked
     *
     * The old contents are released first rather than copied: every caller
     * here overwrites the buffer immediately afterwards, and copying would
     * need both allocations live at once, which is the one thing a constrained
     * part cannot spare. A count that has not changed is left alone, which is
     * what makes this safe to call from a render path every frame.
     */
    bool Allocate(size_t count, MemoryUse use = MemoryUse::Auto, const char* context = nullptr)
    {
        if (count == m_Count && m_Data)
            return true;

        Reset();
        if (count == 0)
            return true;  // an empty buffer is a success, not a failure

        // Checked here rather than in the allocator: a count from a file or a
        // scene is corruption-controlled, and count * sizeof(T) is a 32-bit
        // size_t on the device, so a large count silently wraps to a small one.
        if (count > (SIZE_MAX / sizeof(T)))
            return false;

        m_Data = static_cast<T*>(detail::BufferAllocate(count * sizeof(T), use, context));
        m_Count = m_Data ? count : 0;
        return m_Data != nullptr;
    }

    /// Free now rather than at scope exit.
    void Reset()
    {
        if (m_Data)
        {
            detail::BufferFree(m_Data);
            m_Data = nullptr;
        }
        m_Count = 0;
    }

    /**
     * @brief Give up ownership, returning the raw pointer
     *
     * For handing a buffer to something that will free it itself, or that
     * outlives this object. The caller becomes responsible for
     * Deki::Memory::Free. Rare by design.
     */
    T* Release()
    {
        T* out = m_Data;
        m_Data = nullptr;
        m_Count = 0;
        return out;
    }

    /// True when this holds memory. The only check a caller needs.
    explicit operator bool() const { return m_Data != nullptr; }

    T* Data() { return m_Data; }
    const T* Data() const { return m_Data; }

    /// Elements, not bytes.
    size_t Count() const { return m_Count; }
    size_t Bytes() const { return m_Count * sizeof(T); }
    bool Empty() const { return m_Count == 0; }

    T& operator[](size_t i) { return m_Data[i]; }
    const T& operator[](size_t i) const { return m_Data[i]; }

    T* begin() { return m_Data; }
    T* end() { return m_Data + m_Count; }
    const T* begin() const { return m_Data; }
    const T* end() const { return m_Data + m_Count; }

private:
    T* m_Data = nullptr;
    size_t m_Count = 0;
};

}  // namespace Deki
