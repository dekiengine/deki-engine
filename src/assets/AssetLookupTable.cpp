/**
 * @file AssetLookupTable.cpp
 * @brief Binary asset lookup table implementation
 */

#include <deki/assets/AssetLookupTable.h>
#include <cstring>

namespace Deki
{

// Static members
const uint8_t* AssetLookupTable::s_Data = nullptr;
uint32_t AssetLookupTable::s_Count = 0;

bool AssetLookupTable::Load(const uint8_t* data, size_t size)
{
    if (!data || size < 4)
        return false;

    // Read entry count. memcpy rather than a cast: `data` comes from a file
    // buffer with no alignment guarantee, and an unaligned uint32 load faults
    // on some of the Cortex-M targets this runs on.
    uint32_t count = 0;
    memcpy(&count, data, sizeof(count));

    // Validate size in 64-bit.
    //
    // `count` is attacker-or-corruption controlled and comes straight from the
    // file. Computing `count * sizeof(Entry)` in size_t was safe on 64-bit
    // desktop but wrapped on the 32-bit ESP32 target, where size_t is
    // also 32 bits — a crafted count produced a small product, the check
    // passed, and Lookup then binary-searched far past the buffer.
    const uint64_t expectedSize =
        4ull + static_cast<uint64_t>(count) * sizeof(Entry);
    if (static_cast<uint64_t>(size) < expectedSize)
    {
        s_Data = nullptr;
        s_Count = 0;
        return false;
    }

    // Lookup hands out entries[i].guid as a bare const char*, and callers treat
    // it as a C string. The field is char[37] with the terminator only there by
    // convention, so verify it once here rather than risk every lookup running
    // off the end of an entry.
    const Entry* candidates = reinterpret_cast<const Entry*>(data + 4);
    for (uint32_t i = 0; i < count; ++i)
    {
        if (candidates[i].guid[sizeof(candidates[i].guid) - 1] != '\0')
        {
            s_Data = nullptr;
            s_Count = 0;
            return false;
        }
    }

    s_Count = count;

    // Store pointer to entry array (right after count)
    s_Data = data + 4;

    return true;
}

void AssetLookupTable::Unload()
{
    s_Data = nullptr;
    s_Count = 0;
}

const char* AssetLookupTable::Lookup(uint32_t hash)
{
    if (!s_Data || s_Count == 0)
        return nullptr;

    // Binary search
    const Entry* entries = reinterpret_cast<const Entry*>(s_Data);

    int32_t lo = 0;
    int32_t hi = static_cast<int32_t>(s_Count) - 1;

    while (lo <= hi)
    {
        int32_t mid = lo + (hi - lo) / 2;
        uint32_t midHash = entries[mid].hash;

        if (midHash == hash)
        {
            return entries[mid].guid;
        }
        else if (midHash < hash)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid - 1;
        }
    }

    return nullptr;
}

uint32_t AssetLookupTable::Hash(const char* path, const char* type)
{
    // FNV-1a hash
    uint32_t hash = 2166136261u;  // FNV offset basis

    // Hash path
    while (*path)
    {
        hash ^= static_cast<uint8_t>(*path++);
        hash *= 16777619u;  // FNV prime
    }

    // Hash colon separator
    hash ^= static_cast<uint8_t>(':');
    hash *= 16777619u;

    // Hash type
    while (*type)
    {
        hash ^= static_cast<uint8_t>(*type++);
        hash *= 16777619u;
    }

    return hash;
}

uint32_t AssetLookupTable::HashString(const char* str)
{
    uint32_t hash = 2166136261u;
    while (*str)
    {
        hash ^= static_cast<uint8_t>(*str++);
        hash *= 16777619u;
    }
    return hash;
}

bool AssetLookupTable::ContainsGuid(const char* guid)
{
    if (!s_Data || s_Count == 0 || !guid)
        return false;

    const Entry* entries = reinterpret_cast<const Entry*>(s_Data);
    for (uint32_t i = 0; i < s_Count; i++)
    {
        if (std::strcmp(entries[i].guid, guid) == 0)
            return true;
    }

    return false;
}

}  // namespace Deki
