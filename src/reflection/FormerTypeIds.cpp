#include "reflection/FormerTypeIds.h"

#include <deki/TypeId.h>
#include <deki/reflection/ComponentRegistry.h>

#ifdef DEKI_EDITOR

namespace Deki
{

namespace
{

}  // namespace

uint32_t LegacyComponentTypeId(uint32_t typeId)
{
    if (typeId == 0)
    {
        return 0;
    }

    const ComponentRegistry& reg = ComponentRegistry::Instance();

    // A live component owning this id always wins over a former name.
    if (reg.GetMeta(typeId) != nullptr)
    {
        return typeId;
    }

    // An id written before a rename, such as a package component moving into
    // a namespace. Unlike the "type" string beside it, a bare id has no name
    // to fall back on, so without this the reference resolves to nothing and
    // the dead id is saved again.
    if (const ComponentMeta* meta = reg.GetMetaByFormerTypeId(typeId))
    {
        return meta->typeId;
    }

    return typeId;
}

}  // namespace Deki

#endif  // DEKI_EDITOR
