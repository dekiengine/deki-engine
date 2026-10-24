// GUID generation.

#include <deki/Guid.h>
#include <cstdint>
#include <cstdio>

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

    // Format as UUID-like string (32 hex chars with dashes). snprintf rather
    // than a stringstream: this runs on devices too, and Tactility does not
    // export the iostream code, so an app that used it would not load. Every
    // part is at most 32 bits, so plain unsigned prints it on every target.
    char text[37];
    std::snprintf(text, sizeof(text), "%08x-%04x-%04x-%04x-%08x%04x", static_cast<unsigned>(hash1 >> 32),
                  static_cast<unsigned>((hash1 >> 16) & 0xFFFF), static_cast<unsigned>(hash1 & 0xFFFF),
                  static_cast<unsigned>((hash2 >> 48) & 0xFFFF), static_cast<unsigned>((hash2 >> 16) & 0xFFFFFFFF),
                  static_cast<unsigned>(hash2 & 0xFFFF));
    return text;
}

}  // namespace Deki
