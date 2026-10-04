
#pragma once

#include <string>

namespace Deki
{

/// A GUID (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx) hashed from `seed` with
/// FNV-1a, so the same seed always gives the same GUID. Used for cache GUIDs
/// built from a source asset and its parameters (such as "fontGuid:fontSize").
std::string GenerateDeterministicGuid(const std::string& seed);

}  // namespace Deki
