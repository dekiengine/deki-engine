#pragma once

// Component type ids saved under a name the class no longer has.
//
// ObjectRef and PropertyRef data store a component's type id (the hash of its
// qualified class name) and no name. When a class is renamed, an id saved
// under an old name is mapped to the current class through its
// DEKI_FORMER_NAME markers, so an older project opens unchanged and its next
// save writes the current id.
//
// Editor-only: exported scenes are rewritten by the editor, so a device never
// sees a former id.

#include <cstdint>
#include <string>

namespace Deki
{

/// The current type id for `typeId`: the id of the class that declared it as
/// a former name, or `typeId` unchanged when no class did.
uint32_t LegacyComponentTypeId(uint32_t typeId);

}  // namespace Deki
