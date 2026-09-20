#include "reflection/FormerTypeIds.h"

#include <deki/TypeId.h>
#include <deki/reflection/ComponentRegistry.h>

#ifdef DEKI_EDITOR

namespace Deki
{

namespace
{


// Every GUID a dekiengine package or the engine declared in 0.10, in GUID order.
// A project's own components resolve through the "class" hint in its scene
// files instead, so they need no entry here.

}  // namespace

uint32_t LegacyComponentTypeId(uint32_t typeId)
{
    if (typeId == 0) return 0;

    const ComponentRegistry& reg = ComponentRegistry::Instance();

    // A live component owning this id always wins. Only if nothing answers to
    // it is it worth asking who used to.
    if (reg.GetMeta(typeId) != nullptr)
        return typeId;

    // An id written before a rename - most often 0.16.0 moving every package
    // component into a namespace, which changed every one of these hashes.
    // Unlike the "type" string beside it, a bare id carries no name to fall
    // back on, so without this the reference resolves to nothing and the dead
    // id is written straight back out on the next save.
    if (const ComponentMeta* meta = reg.GetMetaByFormerTypeId(typeId))
        return meta->typeId;

    return typeId;
}

}  // namespace Deki

#endif  // DEKI_EDITOR
