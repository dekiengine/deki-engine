#pragma once

// An owning buffer. Allocates through Deki::Memory and frees itself.
//
// Component and package code should use this instead of calling an allocator
// and remembering the matching Free: the destructor cannot pair the wrong
// free, forget one, or free memory it does not own.
//
// WHY NOT AN ARENA
// An arena must be sized up front, and one big enough for a full-screen
// gradient bake holds 150 KB whether or not a project has a gradient: half the
// heap on some parts. Most allocations here are long-lived and owned anyway
// (glyph tables, pixel data, particle columns, framebuffers), which suits RAII.
//
// The caller still checks that the memory was available: `if (!buf)` once,
// where the buffer is created.

#include <deki/providers/Memory.h>

#include <stddef.h>
#include <utility>
#include <type_traits>

namespace Deki
{

namespace Detail
{
// Not templates on purpose, so each Buffer<T> adds only a cast and a multiply
// rather than its own copy of the allocate and free logic: code size is a
// budget on small parts.
void* BufferAllocate(size_t bytes, Memory::Region region, AllocContext ctx);
void BufferFree(void* ptr);
}  // namespace Detail

/// Owning array of `T`, freed when it goes out of scope.
///
/// Move-only: a copy would need a second allocation that could fail, which a
/// copy constructor cannot report.
///
/// For raw pixel, glyph and sample data handed to blitters and drivers. It
/// never constructs or destroys elements (Memory returns them zeroed), so T
/// must be trivially copyable and destructible; the static_asserts enforce it.
template <typename T>
class Buffer
{
    static_assert(std::is_trivially_copyable<T>::value, "Deki::Buffer<T> is for trivially copyable T; it does not "
                                                        "construct or copy elements");
    static_assert(std::is_trivially_destructible<T>::value, "Deki::Buffer<T> does not destroy elements; a T with a "
                                                            "destructor would leak whatever that destructor releases");

public:
    Buffer() = default;

    /// Allocates `count` elements. Check the result with `if (!buffer)`.
    Buffer(size_t count, Memory::Region region, AllocContext ctx = {}) { Allocate(count, region, ctx); }

    ~Buffer() { Reset(); }

    Buffer(Buffer&& other) noexcept
        : m_Data(other.m_Data),
          m_Count(other.m_Count)
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

    /// Allocates `count` elements, replacing what it held. Returns false on
    /// failure.
    ///
    /// The old contents are freed first, not copied: callers overwrite the
    /// buffer right away, and a copy would need both allocations at once. An
    /// unchanged count keeps the existing memory, so calling this every frame
    /// from a render path is cheap.
    bool Allocate(size_t count, Memory::Region region, AllocContext ctx = {})
    {
        if (count == m_Count && m_Data)
        {
            return true;
        }

        Reset();
        if (count == 0)
        {
            return true;  // an empty buffer is a success, not a failure
        }

        // A count can come from a corrupt file or scene, and count * sizeof(T)
        // is a 32-bit size_t on the device, so a large count would wrap to a
        // small size.
        if (count > (SIZE_MAX / sizeof(T)))
        {
            return false;
        }

        // Pass ctx on, never a new default: it must name whoever called
        // Allocate, not this header.
        m_Data = static_cast<T*>(Detail::BufferAllocate(count * sizeof(T), region, ctx));
        m_Count = m_Data ? count : 0;
        return m_Data != nullptr;
    }

    /// Frees now rather than at scope exit.
    void Reset()
    {
        if (m_Data)
        {
            Detail::BufferFree(m_Data);
            m_Data = nullptr;
        }
        m_Count = 0;
    }

    /// Gives up ownership and returns the raw pointer, for handing the memory
    /// to something that frees it itself or outlives this object. The caller
    /// must then call Deki::Memory::Free. Rarely needed.
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
