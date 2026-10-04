#pragma once

// A growable array whose growth can fail.
//
// With exceptions off, std::vector's push_back aborts when memory runs out,
// and the board reboots. That is fine for the editor, but on a device running
// short of memory is normal, not a bug. So this has std::vector's shape, and
// everything that can allocate returns bool.
//
//     Deki::Vector<Enemy> enemies(Deki::Memory::Internal);
//     if (!enemies.Reserve(32))
//         return;                      // out of memory, said once
//     if (!enemies.PushBack(spawned))  // and again per growth, if it grows
//         DEKI_LOG_WARNING("dropped a spawn");
//
// WHEN TO USE WHICH
//
//   Deki::Buffer<T>  a fixed number of elements, known when you allocate:
//                    pixels, glyphs, spans, a particle pool's columns.
//   Deki::Vector<T>  a count that changes at runtime and has no fixed bound.
//   std::vector<T>   editor and tooling code, where exceptions work and the
//                    heap is a desktop's.
//
// Prefer Buffer. Most "dynamic" data on a device has a bound the project
// already knows, and a fixed allocation does not fragment a small heap the
// way repeated growth does.

#include <deki/providers/Buffer.h>

#include <stddef.h>
#include <string.h>
#include <utility>
#include <type_traits>

namespace Deki
{

/// Growable array of `T` whose growth can fail.
///
/// Move-only, like Buffer: copying needs a second allocation that can fail,
/// and a copy constructor cannot report that.
///
/// T must be trivially copyable and trivially destructible, which Buffer
/// enforces. Growth moves elements with memcpy and destruction just frees the
/// block, so these are lists of plain structs and handles.
template <typename T>
class Vector
{
public:
    /// Takes the memory region the storage lives in. Required: the engine does
    /// not pick a region, so the declaration says what memory this list uses.
    explicit Vector(Memory::Region region, AllocContext ctx = {})
        : m_Region(region),
          m_Context(ctx)
    {
    }

    Vector() = delete;
    ~Vector() = default;

    // Not defaulted: the source must end with m_Size 0, or it would claim
    // elements over an empty Buffer and Back() would dereference null.
    Vector(Vector&& other) noexcept
        : m_Storage(std::move(other.m_Storage)),
          m_Size(other.m_Size),
          m_Region(other.m_Region),
          m_Context(other.m_Context)
    {
        other.m_Size = 0;
    }

    Vector& operator=(Vector&& other) noexcept
    {
        if (this != &other)
        {
            m_Storage = std::move(other.m_Storage);
            m_Size = other.m_Size;
            m_Region = other.m_Region;
            m_Context = other.m_Context;
            other.m_Size = 0;
        }
        return *this;
    }

    Vector(const Vector&) = delete;
    Vector& operator=(const Vector&) = delete;

    /// Makes room for at least `count` elements in the constructor's region.
    /// Call it once up front, where a failure is easy to handle; the PushBack
    /// calls within that capacity then cannot fail. Never shrinks. Returns
    /// true when the capacity is already enough.
    bool Reserve(size_t count)
    {
        if (count <= m_Storage.Count())
        {
            return true;
        }

        Buffer<T> grown;
        if (!grown.Allocate(count, m_Region, m_Context))
        {
            return false;
        }

        // memcpy is safe: Buffer's static_asserts make T trivially copyable.
        if (m_Size > 0)
        {
            memcpy(grown.Data(), m_Storage.Data(), m_Size * sizeof(T));
        }
        m_Storage = std::move(grown);
        return true;
    }

    /// Appends a copy of `value`, growing if needed. Returns false when it
    /// could not grow; the vector is then unchanged.
    ///
    /// Growth doubles (amortised O(1)) from a first block of 4. The first block
    /// is small because a device project has many short lists.
    bool PushBack(const T& value)
    {
        if (m_Size == m_Storage.Count())
        {
            const size_t grown = m_Storage.Count() ? m_Storage.Count() * 2 : kFirstBlock;
            if (grown < m_Storage.Count())  // size_t is 32-bit on the device
            {
                return false;
            }
            if (!Reserve(grown))
            {
                return false;
            }
        }
        m_Storage[m_Size] = value;
        ++m_Size;
        return true;
    }

    /// Drops the last element. False when there was none.
    bool PopBack()
    {
        if (m_Size == 0)
        {
            return false;
        }
        --m_Size;
        return true;
    }

    /// Removes the element at `index` and moves the last one into its place.
    /// O(1) but changes the order, like ParticlePool's KillSwap. Returns false
    /// when `index` is out of range. To keep the order, rebuild the list.
    bool RemoveUnordered(size_t index)
    {
        if (index >= m_Size)
        {
            return false;
        }
        --m_Size;
        if (index != m_Size)
        {
            m_Storage[index] = m_Storage[m_Size];
        }
        return true;
    }

    /// Empties the vector and keeps the memory, for refilling every frame.
    void Clear() { m_Size = 0; }

    /// Empties the vector and frees the memory.
    void Reset()
    {
        m_Storage.Reset();
        m_Size = 0;
    }

    size_t Size() const { return m_Size; }
    size_t Capacity() const { return m_Storage.Count(); }
    bool Empty() const { return m_Size == 0; }

    T* Data() { return m_Storage.Data(); }
    const T* Data() const { return m_Storage.Data(); }

    T& operator[](size_t i) { return m_Storage[i]; }
    const T& operator[](size_t i) const { return m_Storage[i]; }

    T* begin() { return m_Storage.Data(); }
    T* end() { return m_Storage.Data() + m_Size; }
    const T* begin() const { return m_Storage.Data(); }
    const T* end() const { return m_Storage.Data() + m_Size; }

    /// The last element. The vector must not be empty.
    T& Back() { return m_Storage[m_Size - 1]; }
    const T& Back() const { return m_Storage[m_Size - 1]; }

private:
    static constexpr size_t kFirstBlock = 4;

    Buffer<T> m_Storage;
    size_t m_Size = 0;

    // Set by the constructor: every growth lands in the declared region and
    // reports the line the vector was declared on, not the PushBack that
    // grew it.
    Memory::Region m_Region;
    AllocContext m_Context;
};

}  // namespace Deki
