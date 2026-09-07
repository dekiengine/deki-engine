#pragma once

/**
 * @file LegacyTypeNames.h
 * @brief Engine 0.10 component GUIDs, mapped to the class names that replaced them.
 *
 * Until 0.11 a component declared a GUID and scene files referred to it by
 * that string (and ObjectRef / PropertyRef data by its hash). The identity is
 * the qualified class name now. The loaders consult this table when they meet
 * a GUID, so a 0.10 project opens unchanged and its next save writes names.
 *
 * Editor-only (the table is compiled under DEKI_EDITOR): exported scenes are
 * rewritten by the editor, so a device never sees a GUID.
 */

#include <cstdint>
#include <string>

namespace Deki
{

/// The class name a 0.10 component GUID stands for, or nullptr when unknown.
const char* LegacyComponentTypeName(const std::string& guid);

/// A type id that was the hash of a 0.10 GUID, as the hash of the class name
/// that replaced it; ids that are not legacy GUIDs come back unchanged.
uint32_t LegacyComponentTypeId(uint32_t typeId);

/// True when the string has the shape of a component GUID.
bool LooksLikeComponentGuid(const std::string& s);

}  // namespace Deki
