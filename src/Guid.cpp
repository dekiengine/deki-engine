/**
 * @file Guid.cpp
 * @brief GUID generation utilities implementation
 */

#include <deki/Guid.h>
#include <cstdint>
#include <sstream>
#include <iomanip>

namespace Deki
{

std::string GenerateDeterministicGuid(const std::string& seed)
{
    // FNV-1a hash constants
    const uint64_t fnvOffset = 14695981039346656037ULL;
    const uint64_t fnvPrime = 1099511628211ULL;

    uint64_t hash1 = fnvOffset;
    uint64_t hash2 = fnvOffset;

    // Hash the seed string, alternating between two hash values
    for (size_t i = 0; i < seed.size(); i++)
    {
        if (i % 2 == 0)
        {
            hash1 ^= static_cast<uint8_t>(seed[i]);
            hash1 *= fnvPrime;
        }
        else
        {
            hash2 ^= static_cast<uint8_t>(seed[i]);
            hash2 *= fnvPrime;
        }
    }

    // Format as UUID-like string (32 hex chars with dashes)
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << (hash1 >> 32);
    ss << "-";
    ss << std::setw(4) << ((hash1 >> 16) & 0xFFFF);
    ss << "-";
    ss << std::setw(4) << (hash1 & 0xFFFF);
    ss << "-";
    ss << std::setw(4) << ((hash2 >> 48) & 0xFFFF);
    ss << "-";
    ss << std::setw(8) << ((hash2 >> 16) & 0xFFFFFFFF);
    ss << std::setw(4) << (hash2 & 0xFFFF);

    return ss.str();
}

}  // namespace Deki
