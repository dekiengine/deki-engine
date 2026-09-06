/**
 * @file AssetLookupTable.h
 * @brief Binary asset lookup table for embedded runtime
 *
 * Provides O(log n) lookup of "path:type" -> GUID using a sorted binary table.
 * Zero heap allocation - just stores a pointer to the loaded table data.
 *
 * Binary format (created by AssetExporter::ExportAssetLookupTable):
 * - [4 bytes] entry count (uint32_t)
 * - [N entries, each 44 bytes, sorted by hash]:
 *   - [4 bytes] FNV-1a hash of "path:type" (uint32_t)
 *   - [37 bytes] GUID string (null-terminated)
 *   - [3 bytes] padding
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace Deki
{

class AssetLookupTable
{
public:
    /**
     * @brief Load the asset lookup table from memory
     *
     * The data must remain valid for the lifetime of lookups.
     * Typically loaded from SD card or flash memory.
     *
     * @param data Pointer to binary table data
     * @param size Size of data in bytes
     * @return true on success
     */
    static bool Load(const uint8_t* data, size_t size);

    /**
     * @brief Unload the lookup table
     */
    static void Unload();

    /**
     * @brief Check if table is loaded
     */
    static bool IsLoaded() { return s_Data != nullptr; }

    /**
     * @brief Look up GUID by hash (binary search)
     * @param hash FNV-1a hash of "path:type" key
     * @return GUID string or nullptr if not found
     */
    static const char* Lookup(uint32_t hash);

    /**
     * @brief Compute FNV-1a hash of a path:type key
     * @param path Asset path (e.g., "scenes/Demo")
     * @param type Asset type name (e.g., "Scene")
     * @return Hash value
     */
    static uint32_t Hash(const char* path, const char* type);

    /**
     * @brief Compute FNV-1a hash of a string
     * @param str String to hash
     * @return Hash value
     */
    static uint32_t HashString(const char* str);

    /**
     * @brief Get entry count
     */
    static uint32_t GetEntryCount() { return s_Count; }

    /**
     * @brief Check if a GUID exists in the table
     * @param guid GUID string to find
     * @return true if GUID exists as a value in any entry
     *
     * O(n) scan - acceptable for asset loading (not per-frame)
     */
    static bool ContainsGuid(const char* guid);

private:
    // Entry layout (must match AssetExporter)
#pragma pack(push, 1)
    struct Entry
    {
        uint32_t hash;
        char guid[37];    // 36 chars + null terminator
        char padding[3];  // Align to 44 bytes
    };
#pragma pack(pop)
    static_assert(sizeof(Entry) == 44, "Entry must be 44 bytes");

    static const uint8_t* s_Data;
    static uint32_t s_Count;
};

}  // namespace Deki
