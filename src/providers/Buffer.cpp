#include <deki/providers/Buffer.h>

namespace Deki
{
namespace detail
{

// The whole of Buffer's allocation logic, out of the template so a Buffer<T>
// per element type does not emit a copy of it. Everything the template adds is
// a cast and a multiply.

void* BufferAllocate(size_t bytes, Memory::Region region, AllocContext ctx)
{
    return Memory::Allocate(bytes, region, ctx);
}

void BufferFree(void* ptr)
{
    Memory::Free(ptr);
}

}  // namespace detail
}  // namespace Deki
