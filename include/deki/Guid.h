/**
 * @file Guid.h
 * @brief GUID generation utilities
 */

#pragma once

#include <string>

namespace Deki
{

/**
 * @brief Generate a deterministic GUID from a seed string
 *
 * Uses FNV-1a hash to generate a UUID-like string that is always
 * the same for a given seed. Useful for generating cache GUIDs
 * from source asset + parameters (e.g., "fontGuid:fontSize").
 *
 * @param seed The seed string to hash
 * @return A UUID-formatted string (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx)
 */
std::string GenerateDeterministicGuid(const std::string& seed);

}  // namespace Deki
