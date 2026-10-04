#pragma once

#include <string>

namespace Deki
{

namespace Storage
{

// Where a target keeps its files. Internal storage is part of the device
// (a board's flash, an app bundle, the disk next to a desktop build);
// external storage is removable (an SD card).
enum Location
{
    Internal,  // F:/
    External,  // S:/
    Default    // the default filesystem, no prefix
};

// The path prefix for a location, such as "F:/".
const char* GetPrefix(Location location);

// Where a build's exported assets (asset_table.bin, pack_index.bin and the
// assets) live on that storage: F:/assets/ inside, the card's root outside.
const char* AssetRoot(Location location);

// Prefix + filename.
std::string BuildPath(Location location, const char* filename);

}  // namespace Storage

}  // namespace Deki
