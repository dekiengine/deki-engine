#pragma once

// A growable array whose growth can fail.
//
// std::vector cannot do that. push_back calls operator new, which with
// exceptions off has no way to report exhaustion — the engine's replacement
// makes it say how many bytes it wanted, but it still aborts, and the board
// still reboots. That is fine for the editor and wrong for a device, where
// running short of memory is a normal operating condition rather than a bug.
//
// So this is std::vector's shape with the one thing changed that matters
// here: everything that can allocate returns bool.
//
//     Deki::Vector<Enemy> enemies(Deki::Internal);
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
// Reach for Buffer first. Most "dynamic" data on a device turns out to have a
// bound the project already knows, and a fixed allocation cannot fragment a
// small heap the way repeated growth can.

#include <deki/providers/Buffer.h>

#include <stddef.h>
#include <string.h>
#include <utility>
#include <type_traits>

namespace Deki
{

/**
 * @brief Growable array of `T` with failable growth
 *
 * Move-only, like Buffer, and for the same reason: copying needs a second
 * allocation that can fail, which a copy constructor cannot report.
 *
 * T must be trivially copyable and trivially destructible — the same
 * constraint Buffer imposes, enforced by it. Growth moves elements with a
 * memcpy and destruction frees the block, so a T with a destructor or a
 * non-trivial move would be silently mishandled. These are the plain structs
 * and handles device code keeps lists of.
 */
template <typename T>
class Vector
{
public:
    /**
     * @brief Construct with the region this vector's storage lives in
     *
     * Required, and there is no default constructor: a vector that grows has
     * to place the new block somewhere, and the engine does not choose. Said
     * once here rather than on every Reserve, so the declaration is where a
     * reader learns what memory this list occupies.
     */
    explicit Vector(MemoryRegion region, AllocContext ctx = {})
        : m_Region(region), m_Context(ctx)
    {
    }

    Vector() = delete;
    ~Vector() = default;

    // Written out rather than defaulted: a defaulted move would carry m_Size
    // across and leave the source still claiming that many elements over a
    // Buffer that no longer holds any, so Back() on a moved-from Vector would
    // dereference null.
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

    /**
     * @brief Make room for at least `count` elements
     *
     * Say this once, up front, where a failure is easy to handle — then the
     * PushBack calls that follow cannot fail. That is the pattern worth
     * writing on a device: one check instead of one per element.
     *
     * Never shrinks. Returns true when the capacity is already sufficient.
     *
     * Placement comes from the constructor, so a growth inside PushBack lands
     * in the same region as the storage it replaces.
     */
    bool Reserve(size_t count)
    {
        if (count <= m_Storage.Count())
            return true;

        Buffer<T> grown;
        if (!grown.Allocate(count, m_Region, m_Context))
            return false;

        // memcpy, not a loop of moves: T is trivially copyable, which is what
        // the static_asserts in Buffer are there to guarantee.
        if (m_Size > 0)
            memcpy(grown.Data(), m_Storage.Data(), m_Size * sizeof(T));
        m_Storage = std::move(grown);
        return true;
    }

    /**
     * @brief Append a copy of `value`, growing if needed
     * @return false when it had to grow and could not; the vector is
     *         unchanged and still holds everything it did before
     *
     * Growth doubles, from a first block of 4. Doubling is what keeps
     * appending amortised O(1); the small first block is because a device
     * project has many short lists and one 16-element default each would add
     * up to more than the lists do.
     */
    bool PushBack(const T& value)
    {
        if (m_Size == m_Storage.Count())
        {
            const size_t grown = m_Storage.Count() ? m_Storage.Count() * 2 : kFirstBlock;
            if (grown < m_Storage.Count())  // size_t is 32-bit on the device
                return false;
            if (!Reserve(grown))
                return false;
        }
        m_Storage[m_Size] = value;
        ++m_Size;
        return true;
    }

    /// Drops the last element. False when there was none.
    bool PopBack()
    {
        if (m_Size == 0)
            return false;
        --m_Size;
        return true;
    }

    /**
     * @brief Remove the element at `index`, filling the hole with the last one
     *
     * O(1), and it does not preserve order — which is what a device wants for
     * the lists it keeps (live entities, active timers) and is the same trick
     * ParticlePool's KillSwap uses. For order-preserving removal, rebuild.
     */
    bool RemoveUnordered(size_t index)
    {
        if (index >= m_Size)
            return false;
        --m_Size;
        if (index != m_Size)
            m_Storage[index] = m_Storage[m_Size];
        return true;
    }

    /// Forget the elements, keep the memory. For refilling every frame.
    void Clear() { m_Size = 0; }

    /// Forget the elements and release the memory.
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

    /// Elements, not capacity — so `for (auto& x : v)` sees what Size() says.
    T& Back() { return m_Storage[m_Size - 1]; }
    const T& Back() const { return m_Storage[m_Size - 1]; }

private:
    static constexpr size_t kFirstBlock = 4;

    Buffer<T> m_Storage;
    size_t m_Size = 0;

    // From the constructor: every growth lands in the region the declaration
    // named, and reports the line the vector was declared on rather than the
    // Reserve inside PushBack that happened to trip it. No member default,
    // because that would be the engine choosing.
    MemoryRegion m_Region;
    AllocContext m_Context;
};

}  // namespace Deki
