// The runtime's "path:type" -> GUID table: a binary search over a sorted
// table, with no heap allocation (it only keeps a pointer to the data).
//
// Format, written by AssetExporter::ExportAssetLookupTable:
// - [4 bytes] entry count (uint32_t)
// - [N entries, each 44 bytes, sorted by hash]:
//   - [4 bytes] FNV-1a hash of "path:type" (uint32_t)
//   - [37 bytes] GUID string (null-terminated)
//   - [3 bytes] padding

#pragma once

#include <cstdint>
#include <cstddef>

namespace Deki
{

class AssetLookupTable
{
public:
    /// Uses the table in `data`, which must stay valid while lookups run (it
    /// is not copied). Returns false when the data is not a valid table.
    static bool Load(const uint8_t* data, size_t size);

    static void Unload();

    static bool IsLoaded() { return s_Data != nullptr; }

    /// The GUID for `hash` (from Hash()), or nullptr when not found.
    static const char* Lookup(uint32_t hash);

    /// FNV-1a hash of the key "path:type", such as "scenes/Demo:Scene".
    static uint32_t Hash(const char* path, const char* type);

    /// FNV-1a hash of `str`.
    static uint32_t HashString(const char* str);

    static uint32_t GetEntryCount() { return s_Count; }

    /// True when any entry holds `guid`. A linear scan: fine at asset load,
    /// too slow per frame.
    static bool ContainsGuid(const char* guid);

private:
    // Must match what AssetExporter writes.
#pragma pack(push, 1)
    struct Entry
    {
        uint32_t hash;
        char guid[37];    // 36 chars + null terminator
        char padding[3];  // pads the entry to 44 bytes
    };
#pragma pack(pop)
    static_assert(sizeof(Entry) == 44, "Entry must be 44 bytes");

    static const uint8_t* s_Data;
    static uint32_t s_Count;
};

}  // namespace Deki
